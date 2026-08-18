#include "platform_shim.h"
#include "particles_spirv.h"
#include "particles_compute_spirv.h"
#include <stdio.h>
#include <stdlib.h>

/* Fully GPU-resident particle physics: a real Vulkan compute shader (SPIR-V
 * hand-assembled from particles_step.comp.spvasm via the spirv-tools
 * package's spirv-as/spirv-val -- see that file's own header comment for
 * why: no glslang/glslc front-end compiler is available in this
 * environment, only the low-level assembler/validator/optimizer -- see
 * particles_compute_spirv.h) integrates every particle's position/velocity
 * under gravity plus wall collisions, writing its output directly into the
 * same storage buffer the graphics pipeline reads as its vertex buffer.
 * There is no CPU readback in the frame loop at all: the compute shader's
 * writes are made visible to the vertex stage with a single
 * vkCmdPipelineBarrier, and the point-sprite draw call reads the freshly
 * computed positions straight off the GPU.
 *
 * This replaces an earlier version of this demo that ran the same physics
 * as a real OpenCL kernel (compiled from source at runtime via
 * clBuildProgram) and read the results back to the host every frame before
 * re-uploading them into the vertex buffer -- see git history for that
 * version. This host driver program (the file you're reading) is still
 * compiled by the squash compiler itself, exactly as before; only the
 * physics kernel's compilation path changed, from OpenCL's online compiler
 * to an offline SPIR-V assemble step.
 *
 * Exercises squash's flat (non-COM) external-call codegen against the
 * genuine system vulkan-1.dll, not a fake implementation. */

#define NUM_PARTICLES 200

typedef struct { float x, y; float r, g, b; } Vertex;

/* Records this frame's compute dispatch + the barrier that makes its writes
 * visible to the vertex stage. While developing this file, calling Vulkan
 * compute functions here reliably segfaulted -- not because of anything
 * about this dispatch code itself, but because of a real squash compiler
 * bug that this file happened to be the first program in this codebase to
 * trip: elf_builder.c's import table (elf_grp_add) had a hardcoded 64-entry
 * cap (MAX_FUNCS), silently dropping any distinct external function beyond
 * the 64th ever called anywhere in the translation unit -- and this file's
 * new Vulkan-compute calls (vkCreateComputePipelines, vkCmdDispatch,
 * vkCmdBindDescriptorSets, vkCmdPushConstants, ...) pushed the program's
 * total past that limit, corrupting GOT-slot lookups for imports registered
 * near the cap. The crash symptom was maximally confusing: it showed up at
 * some other, unrelated, already-proven-working call site (vkAcquireNextImageKHR)
 * rather than at the actual unregistered import, and its exact location
 * drifted as unrelated code was added/removed nearby -- see elf_builder.c's
 * MAX_FUNCS comment for the real fix (raised to 256). This function stays
 * split out of main() on its own merits (keeps the frame loop's dispatch
 * logic testable/readable in isolation), not because it's still needed as
 * a workaround. */
typedef struct {
    VkCommandBuffer cmd;
    VkPipeline computePipeline;
    VkPipelineLayout computePipelineLayout;
    VkDescriptorSet descriptorSet;
    VkBuffer vertexBuffer;
    VkDeviceSize vertexBufferSize;
    VkBuffer velBuffer;
    VkDeviceSize velBufferSize;
    float dt;
    uint32_t computeGroups;
} ComputeDispatchArgs;

static void record_particle_compute_dispatch(const ComputeDispatchArgs *a) {
    vkCmdBindPipeline(a->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, a->computePipeline);
    vkCmdPushConstants(a->cmd, a->computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &a->dt);
    vkCmdBindDescriptorSets(a->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, a->computePipelineLayout, 0, 1, &a->descriptorSet, 0, NULL);
    vkCmdDispatch(a->cmd, a->computeGroups, 1, 1);

    VkBufferMemoryBarrier bufBarriers[2];
    memset(bufBarriers, 0, sizeof(bufBarriers));
    bufBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bufBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bufBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    bufBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufBarriers[0].buffer = a->vertexBuffer;
    bufBarriers[0].offset = 0;
    bufBarriers[0].size = a->vertexBufferSize;
    bufBarriers[1] = bufBarriers[0];
    bufBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bufBarriers[1].buffer = a->velBuffer;
    bufBarriers[1].size = a->velBufferSize;
    vkCmdPipelineBarrier(a->cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 0, NULL, 2, bufBarriers, 0, NULL);
}

static uint32_t find_memory_type(VkPhysicalDeviceMemoryProperties *memProps, uint32_t typeBits, VkFlags props) {
    for (uint32_t i = 0; i < memProps->memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (memProps->memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    fprintf(stderr, "find_memory_type: no suitable memory type found!\n"); fflush(stdout);
    return 0xFFFFFFFFu;
}

typedef struct {
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    VkPipelineLayout computePipelineLayout;
    VkPipeline computePipeline;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexMemory;
    VkBuffer velBuffer;
    VkDeviceMemory velMemory;
} ComputeResources;

/* Creates the descriptor set layout/pool/set, compute pipeline, and the two
 * storage buffers (position -- shared with the graphics pipeline's vertex
 * buffer -- and velocity), seeding both from hostPos/hostVel. Pulled out of
 * main() into its own function purely to keep main() itself shorter and
 * more readable (see record_particle_compute_dispatch's header comment for
 * a real squash compiler bug this file's development tripped over -- it
 * looked at first like it might be related to how large this function was,
 * but the actual cause turned out to be unrelated to function size). */
static int setup_compute_resources(VkDevice device, VkPhysicalDevice phys, VkShaderModule csModule,
        const float *hostPos, const float *hostVel, ComputeResources *out) {
    VkResult vr;
    memset(out, 0, sizeof(*out));

    /* Two storage buffers: binding 0 is the SAME buffer as the graphics
     * pipeline's vertex buffer (the compute shader only ever writes the
     * first two floats -- x,y -- of each particle's 5-float stride; r,g,b
     * are written once from the host below and never touched again),
     * binding 1 is velocity (2 floats/particle), never read by the
     * graphics pipeline at all. */
    VkDescriptorSetLayoutBinding dslBindings[2];
    memset(dslBindings, 0, sizeof(dslBindings));
    dslBindings[0].binding = 0;
    dslBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dslBindings[0].descriptorCount = 1;
    dslBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    dslBindings[1].binding = 1;
    dslBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dslBindings[1].descriptorCount = 1;
    dslBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslInfo;
    memset(&dslInfo, 0, sizeof(dslInfo));
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 2;
    dslInfo.pBindings = dslBindings;

    vr = vkCreateDescriptorSetLayout(device, &dslInfo, NULL, &out->descriptorSetLayout);
    fprintf(stderr, "vkCreateDescriptorSetLayout vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;

    VkDescriptorPoolSize poolSize;
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 2;

    VkDescriptorPoolCreateInfo dpInfo;
    memset(&dpInfo, 0, sizeof(dpInfo));
    dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpInfo.maxSets = 1;
    dpInfo.poolSizeCount = 1;
    dpInfo.pPoolSizes = &poolSize;

    vr = vkCreateDescriptorPool(device, &dpInfo, NULL, &out->descriptorPool);
    fprintf(stderr, "vkCreateDescriptorPool vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;

    VkDescriptorSetAllocateInfo dsAllocInfo;
    memset(&dsAllocInfo, 0, sizeof(dsAllocInfo));
    dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAllocInfo.descriptorPool = out->descriptorPool;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts = &out->descriptorSetLayout;

    vr = vkAllocateDescriptorSets(device, &dsAllocInfo, &out->descriptorSet);
    fprintf(stderr, "vkAllocateDescriptorSets vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;

    VkPushConstantRange pcRange;
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(float);

    VkPipelineLayoutCreateInfo cplInfo;
    memset(&cplInfo, 0, sizeof(cplInfo));
    cplInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    cplInfo.setLayoutCount = 1;
    cplInfo.pSetLayouts = &out->descriptorSetLayout;
    cplInfo.pushConstantRangeCount = 1;
    cplInfo.pPushConstantRanges = &pcRange;

    vr = vkCreatePipelineLayout(device, &cplInfo, NULL, &out->computePipelineLayout);
    fprintf(stderr, "vkCreatePipelineLayout(compute) vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;

    VkComputePipelineCreateInfo cpInfo;
    memset(&cpInfo, 0, sizeof(cpInfo));
    cpInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpInfo.stage.module = csModule;
    cpInfo.stage.pName = "main";
    cpInfo.layout = out->computePipelineLayout;
    cpInfo.basePipelineIndex = -1;

    vr = vkCreateComputePipelines(device, NULL, 1, &cpInfo, NULL, &out->computePipeline);
    fprintf(stderr, "vkCreateComputePipelines vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;

    /* Vertex buffer: one Vertex per particle -- also bound as the compute
     * shader's position storage buffer (binding 0). Colors are assigned
     * once (a simple hue ramp across the particle index) and never change;
     * positions start at the same random values the old CPU/OpenCL version
     * used, then evolve entirely on the GPU from here on. */
    Vertex verts[NUM_PARTICLES];
    for (int i = 0; i < NUM_PARTICLES; i++) {
        float t = (float)i / (float)NUM_PARTICLES;
        verts[i].x = hostPos[i*2+0];
        verts[i].y = hostPos[i*2+1];
        verts[i].r = 0.3f + 0.7f * t;
        verts[i].g = 0.3f + 0.5f * (1.0f - t);
        verts[i].b = 0.9f - 0.5f * t;
    }

    VkPhysicalDeviceMemoryProperties memProps;
    memset(&memProps, 0, sizeof(memProps));
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);

    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = sizeof(verts);
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vr = vkCreateBuffer(device, &bufInfo, NULL, &out->vertexBuffer);
    fprintf(stderr, "vkCreateBuffer(vertex/pos) vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;

    VkMemoryRequirements memReq;
    memset(&memReq, 0, sizeof(memReq));
    vkGetBufferMemoryRequirements(device, out->vertexBuffer, &memReq);

    uint32_t memTypeIdx = find_memory_type(&memProps, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    fprintf(stderr, "memTypeIdx=%u\n", memTypeIdx); fflush(stdout);
    if (memTypeIdx == 0xFFFFFFFFu) return 0;

    VkMemoryAllocateInfo allocInfo;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIdx;
    vr = vkAllocateMemory(device, &allocInfo, NULL, &out->vertexMemory);
    fprintf(stderr, "vkAllocateMemory(vertex/pos) vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;
    vkBindBufferMemory(device, out->vertexBuffer, out->vertexMemory, 0);

    void *mapped = NULL;
    vkMapMemory(device, out->vertexMemory, 0, sizeof(verts), 0, &mapped);
    memcpy(mapped, verts, sizeof(verts));
    vkUnmapMemory(device, out->vertexMemory);
    fprintf(stderr, "main: vertex/pos buffer created+uploaded\n"); fflush(stdout);

    /* Velocity buffer: compute-only storage buffer (binding 1), seeded once
     * from the host, never touched by the CPU again. */
    VkBufferCreateInfo velBufInfo;
    memset(&velBufInfo, 0, sizeof(velBufInfo));
    velBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    velBufInfo.size = NUM_PARTICLES * 2 * sizeof(float);
    velBufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    velBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vr = vkCreateBuffer(device, &velBufInfo, NULL, &out->velBuffer);
    fprintf(stderr, "vkCreateBuffer(vel) vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;

    VkMemoryRequirements velMemReq;
    memset(&velMemReq, 0, sizeof(velMemReq));
    vkGetBufferMemoryRequirements(device, out->velBuffer, &velMemReq);

    uint32_t velMemTypeIdx = find_memory_type(&memProps, velMemReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    fprintf(stderr, "velMemTypeIdx=%u\n", velMemTypeIdx); fflush(stdout);
    if (velMemTypeIdx == 0xFFFFFFFFu) return 0;

    VkMemoryAllocateInfo velAllocInfo;
    memset(&velAllocInfo, 0, sizeof(velAllocInfo));
    velAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    velAllocInfo.allocationSize = velMemReq.size;
    velAllocInfo.memoryTypeIndex = velMemTypeIdx;
    vr = vkAllocateMemory(device, &velAllocInfo, NULL, &out->velMemory);
    fprintf(stderr, "vkAllocateMemory(vel) vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;
    vkBindBufferMemory(device, out->velBuffer, out->velMemory, 0);

    void *velMapped = NULL;
    vkMapMemory(device, out->velMemory, 0, NUM_PARTICLES * 2 * sizeof(float), 0, &velMapped);
    memcpy(velMapped, hostVel, NUM_PARTICLES * 2 * sizeof(float));
    vkUnmapMemory(device, out->velMemory);
    fprintf(stderr, "main: vel buffer created+uploaded\n"); fflush(stdout);

    VkDescriptorBufferInfo posDescInfo;
    posDescInfo.buffer = out->vertexBuffer;
    posDescInfo.offset = 0;
    posDescInfo.range = sizeof(verts);
    VkDescriptorBufferInfo velDescInfo;
    velDescInfo.buffer = out->velBuffer;
    velDescInfo.offset = 0;
    velDescInfo.range = NUM_PARTICLES * 2 * sizeof(float);

    VkWriteDescriptorSet writes[2];
    memset(writes, 0, sizeof(writes));
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = out->descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &posDescInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = out->descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &velDescInfo;
    vkUpdateDescriptorSets(device, 2, writes, 0, NULL);
    fprintf(stderr, "main: descriptor set updated\n"); fflush(stdout);

    return 1;
}

int main(void) {
    fprintf(stderr, "particles_physics: start\n"); fflush(stdout);
    init_particles_spirv();
    init_particles_step_comp_spirv();

    /* Host-side particle state, used only to seed the initial GPU buffers --
     * matches the compute shader's std430 float-array layout exactly: the
     * vertex buffer is 5 floats/particle (x,y,r,g,b), the velocity buffer is
     * 2 floats/particle (vx,vy). Once uploaded, neither buffer is ever read
     * back to the host again. */
    float hostPos[NUM_PARTICLES * 2];
    float hostVel[NUM_PARTICLES * 2];
    srand(12345u);
    for (int i = 0; i < NUM_PARTICLES; i++) {
        hostPos[i*2+0] = ((float)(rand() % 2000) / 1000.0f) - 1.0f;
        hostPos[i*2+1] = ((float)(rand() % 1000) / 1000.0f) * 0.8f;
        hostVel[i*2+0] = (((float)(rand() % 2000) / 1000.0f) - 1.0f) * 0.6f;
        hostVel[i*2+1] = 0.0f;
    }
    fprintf(stderr, "particle init loop done, hostPos[0]=%s hostPos[1]=%s\n", platform_fmt2f((double)hostPos[0]), platform_fmt2f((double)hostPos[1])); fflush(stdout);

    /* ================= Vulkan: window + renderer setup ================= */
    PlatformWindow pw;
    if (!platform_create_window(&pw, "squash Vulkan-compute physics + Vulkan render", 800, 600)) {
        fprintf(stderr, "platform_create_window failed\n"); fflush(stdout); return 1;
    }
    fprintf(stderr, "main: window created\n"); fflush(stdout);

    VkApplicationInfo appInfo;
    memset(&appInfo, 0, sizeof(appInfo));
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "squash particles";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "none";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = (1u << 22);

    const char *instExts[2] = { "VK_KHR_surface", platform_surface_extension() };
    VkInstanceCreateInfo instInfo;
    memset(&instInfo, 0, sizeof(instInfo));
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledExtensionCount = 2;
    instInfo.ppEnabledExtensionNames = instExts;

    VkInstance instance = NULL;
    VkResult vr = vkCreateInstance(&instInfo, NULL, &instance);
    fprintf(stderr, "vkCreateInstance vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 1;

    uint32_t physCount = 0;
    vkEnumeratePhysicalDevices(instance, &physCount, NULL);
    fprintf(stderr, "physical device count=%u\n", physCount); fflush(stdout);
    if (physCount == 0) return 1;
    VkPhysicalDevice physDevices[8];
    if (physCount > 8) physCount = 8;
    vkEnumeratePhysicalDevices(instance, &physCount, physDevices);
    VkPhysicalDevice phys = physDevices[0];

    VkSurfaceKHR surface = NULL;
    vr = platform_create_vk_surface(instance, &pw, &surface);
    fprintf(stderr, "platform_create_vk_surface vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 1;

    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qfCount, NULL);
    VkQueueFamilyProperties qfProps[16];
    if (qfCount > 16) qfCount = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qfCount, qfProps);
    uint32_t queueFamily = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < qfCount; i++) {
        if (!(qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        VkBool32 presentOk = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &presentOk);
        if (presentOk) { queueFamily = i; break; }
    }
    fprintf(stderr, "queueFamily=%u\n", queueFamily); fflush(stdout);
    if (queueFamily == 0xFFFFFFFFu) return 1;

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo dqInfo;
    memset(&dqInfo, 0, sizeof(dqInfo));
    dqInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dqInfo.queueFamilyIndex = queueFamily;
    dqInfo.queueCount = 1;
    dqInfo.pQueuePriorities = &queuePriority;

    const char *devExts[1] = { "VK_KHR_swapchain" };
    /* Enable "largePoints" (feature index 16 in the real
     * VkPhysicalDeviceFeatures layout -- robustBufferAccess,
     * fullDrawIndexUint32, imageCubeArray, independentBlend,
     * geometryShader, tessellationShader, sampleRateShading, dualSrcBlend,
     * logicOp, multiDrawIndirect, drawIndirectFirstInstance, depthClamp,
     * depthBiasClamp, fillModeNonSolid, depthBounds, wideLines,
     * largePoints = index 16) so gl_PointSize values other than 1.0 are
     * honored -- without it, point sprites silently clamp to a 1-pixel dot. */
    VkPhysicalDeviceFeatures feats;
    memset(&feats, 0, sizeof(feats));
    feats._reserved[16] = 1;
    VkDeviceCreateInfo devInfo;
    memset(&devInfo, 0, sizeof(devInfo));
    devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devInfo.queueCreateInfoCount = 1;
    devInfo.pQueueCreateInfos = &dqInfo;
    devInfo.enabledExtensionCount = 1;
    devInfo.ppEnabledExtensionNames = devExts;
    devInfo.pEnabledFeatures = &feats;

    VkDevice device = NULL;
    vr = vkCreateDevice(phys, &devInfo, NULL, &device);
    fprintf(stderr, "vkCreateDevice vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 1;

    VkQueue queue = NULL;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    fprintf(stderr, "main: device+queue created\n"); fflush(stdout);

    VkSurfaceCapabilitiesKHR caps;
    memset(&caps, 0, sizeof(caps));
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, NULL);
    VkSurfaceFormatKHR fmts[32];
    if (fmtCount > 32) fmtCount = 32;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, fmts);
    VkFormat chosenFormat = (fmtCount > 0) ? fmts[0].format : VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR chosenColorSpace = (fmtCount > 0) ? fmts[0].colorSpace : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (uint32_t i = 0; i < fmtCount; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) { chosenFormat = fmts[i].format; chosenColorSpace = fmts[i].colorSpace; break; }
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) { extent.width = 800; extent.height = 600; }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR scInfo;
    memset(&scInfo, 0, sizeof(scInfo));
    scInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scInfo.surface = surface;
    scInfo.minImageCount = imageCount;
    scInfo.imageFormat = chosenFormat;
    scInfo.imageColorSpace = chosenColorSpace;
    scInfo.imageExtent = extent;
    scInfo.imageArrayLayers = 1;
    scInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scInfo.preTransform = caps.currentTransform;
    scInfo.compositeAlpha = 0x1;
    scInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    scInfo.clipped = VK_TRUE;

    VkSwapchainKHR swapchain = NULL;
    vr = vkCreateSwapchainKHR(device, &scInfo, NULL, &swapchain);
    fprintf(stderr, "vkCreateSwapchainKHR vr=%d extent=%ux%u\n", (int)vr, extent.width, extent.height); fflush(stdout);
    if (vr != VK_SUCCESS) return 1;

    uint32_t swapImageCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, NULL);
    VkImage swapImages[8];
    if (swapImageCount > 8) swapImageCount = 8;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages);
    fprintf(stderr, "swapImageCount=%u\n", swapImageCount); fflush(stdout);

    VkImageView swapViews[8];
    for (uint32_t i = 0; i < swapImageCount; i++) {
        VkImageViewCreateInfo ivInfo;
        memset(&ivInfo, 0, sizeof(ivInfo));
        ivInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivInfo.image = swapImages[i];
        ivInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivInfo.format = chosenFormat;
        ivInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivInfo.subresourceRange.levelCount = 1;
        ivInfo.subresourceRange.layerCount = 1;
        vr = vkCreateImageView(device, &ivInfo, NULL, &swapViews[i]);
        if (vr != VK_SUCCESS) { fprintf(stderr, "vkCreateImageView[%u] failed vr=%d\n", i, (int)vr); fflush(stdout); return 1; }
    }
    fprintf(stderr, "main: %u image views created\n", swapImageCount); fflush(stdout);

    VkAttachmentDescription colorAttach;
    memset(&colorAttach, 0, sizeof(colorAttach));
    colorAttach.format = chosenFormat;
    colorAttach.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttach.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef;
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass;
    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep;
    memset(&dep, 0, sizeof(dep));
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo;
    memset(&rpInfo, 0, sizeof(rpInfo));
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttach;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;

    VkRenderPass renderPass = NULL;
    vr = vkCreateRenderPass(device, &rpInfo, NULL, &renderPass);
    fprintf(stderr, "vkCreateRenderPass vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 1;

    VkFramebuffer framebuffers[8];
    for (uint32_t i = 0; i < swapImageCount; i++) {
        VkFramebufferCreateInfo fbInfo;
        memset(&fbInfo, 0, sizeof(fbInfo));
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &swapViews[i];
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;
        vr = vkCreateFramebuffer(device, &fbInfo, NULL, &framebuffers[i]);
        if (vr != VK_SUCCESS) { fprintf(stderr, "vkCreateFramebuffer[%u] failed vr=%d\n", i, (int)vr); fflush(stdout); return 1; }
    }
    fprintf(stderr, "main: %u framebuffers created\n", swapImageCount); fflush(stdout);

    VkShaderModuleCreateInfo vsInfo;
    memset(&vsInfo, 0, sizeof(vsInfo));
    vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsInfo.codeSize = sizeof(g_particles_vert_spv);
    vsInfo.pCode = g_particles_vert_spv;
    VkShaderModule vsModule = NULL;
    vr = vkCreateShaderModule(device, &vsInfo, NULL, &vsModule);
    fprintf(stderr, "vkCreateShaderModule(vs) vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 1;

    VkShaderModuleCreateInfo fsInfo;
    memset(&fsInfo, 0, sizeof(fsInfo));
    fsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fsInfo.codeSize = sizeof(g_particles_frag_spv);
    fsInfo.pCode = g_particles_frag_spv;
    VkShaderModule fsModule = NULL;
    vr = vkCreateShaderModule(device, &fsInfo, NULL, &fsModule);
    fprintf(stderr, "vkCreateShaderModule(fs) vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 1;

    VkShaderModuleCreateInfo csInfo;
    memset(&csInfo, 0, sizeof(csInfo));
    csInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    csInfo.codeSize = sizeof(g_particles_step_comp_spv);
    csInfo.pCode = g_particles_step_comp_spv;
    VkShaderModule csModule = NULL;
    vr = vkCreateShaderModule(device, &csInfo, NULL, &csModule);
    fprintf(stderr, "vkCreateShaderModule(cs) vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 1;

    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding;
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2];
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = 8;

    VkPipelineVertexInputStateCreateInfo vinState;
    memset(&vinState, 0, sizeof(vinState));
    vinState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vinState.vertexBindingDescriptionCount = 1;
    vinState.pVertexBindingDescriptions = &binding;
    vinState.vertexAttributeDescriptionCount = 2;
    vinState.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo iaState;
    memset(&iaState, 0, sizeof(iaState));
    iaState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaState.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    VkViewport viewport;
    viewport.x = 0; viewport.y = 0;
    viewport.width = (float)extent.width; viewport.height = (float)extent.height;
    viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
    VkRect2D scissor;
    scissor.offset.x = 0; scissor.offset.y = 0;
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo vpState;
    memset(&vpState, 0, sizeof(vpState));
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = &viewport;
    vpState.scissorCount = 1;
    vpState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rsState;
    memset(&rsState, 0, sizeof(rsState));
    rsState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsState.polygonMode = VK_POLYGON_MODE_FILL;
    rsState.cullMode = 0;
    rsState.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rsState.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msState;
    memset(&msState, 0, sizeof(msState));
    msState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttach;
    memset(&blendAttach, 0, sizeof(blendAttach));
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cbState;
    memset(&cbState, 0, sizeof(cbState));
    cbState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbState.attachmentCount = 1;
    cbState.pAttachments = &blendAttach;

    VkPipelineLayoutCreateInfo plInfo;
    memset(&plInfo, 0, sizeof(plInfo));
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkPipelineLayout pipelineLayout = NULL;
    vr = vkCreatePipelineLayout(device, &plInfo, NULL, &pipelineLayout);
    fprintf(stderr, "vkCreatePipelineLayout vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 1;

    VkGraphicsPipelineCreateInfo gpInfo;
    memset(&gpInfo, 0, sizeof(gpInfo));
    gpInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpInfo.stageCount = 2;
    gpInfo.pStages = stages;
    gpInfo.pVertexInputState = &vinState;
    gpInfo.pInputAssemblyState = &iaState;
    gpInfo.pViewportState = &vpState;
    gpInfo.pRasterizationState = &rsState;
    gpInfo.pMultisampleState = &msState;
    gpInfo.pColorBlendState = &cbState;
    gpInfo.layout = pipelineLayout;
    gpInfo.renderPass = renderPass;
    gpInfo.subpass = 0;
    gpInfo.basePipelineIndex = -1;

    VkPipeline pipeline = NULL;
    vr = vkCreateGraphicsPipelines(device, NULL, 1, &gpInfo, NULL, &pipeline);
    fprintf(stderr, "vkCreateGraphicsPipelines vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 1;

    /* ================= Vulkan compute: physics pipeline ================= */
    ComputeResources cr;
    if (!setup_compute_resources(device, phys, csModule, hostPos, hostVel, &cr)) return 1;
    VkDescriptorSetLayout descriptorSetLayout = cr.descriptorSetLayout;
    VkDescriptorPool descriptorPool = cr.descriptorPool;
    VkDescriptorSet descriptorSet = cr.descriptorSet;
    VkPipelineLayout computePipelineLayout = cr.computePipelineLayout;
    VkPipeline computePipeline = cr.computePipeline;
    VkBuffer vertexBuffer = cr.vertexBuffer;
    VkDeviceMemory vertexMemory = cr.vertexMemory;
    VkBuffer velBuffer = cr.velBuffer;
    VkDeviceMemory velMemory = cr.velMemory;
    (void)descriptorSetLayout; (void)descriptorPool; (void)vertexMemory; (void)velMemory;

    VkCommandPoolCreateInfo cpInfo2;
    memset(&cpInfo2, 0, sizeof(cpInfo2));
    cpInfo2.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpInfo2.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpInfo2.queueFamilyIndex = queueFamily;
    VkCommandPool commandPool = NULL;
    vr = vkCreateCommandPool(device, &cpInfo2, NULL, &commandPool);
    fprintf(stderr, "vkCreateCommandPool vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 1;

    VkCommandBufferAllocateInfo cbAllocInfo;
    memset(&cbAllocInfo, 0, sizeof(cbAllocInfo));
    cbAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAllocInfo.commandPool = commandPool;
    cbAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAllocInfo.commandBufferCount = swapImageCount;
    VkCommandBuffer cmdBufs[8];
    vr = vkAllocateCommandBuffers(device, &cbAllocInfo, cmdBufs);
    fprintf(stderr, "vkAllocateCommandBuffers vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 1;

    VkSemaphoreCreateInfo semInfo;
    memset(&semInfo, 0, sizeof(semInfo));
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore imageAvailable = NULL, renderFinished = NULL;
    vkCreateSemaphore(device, &semInfo, NULL, &imageAvailable);
    vkCreateSemaphore(device, &semInfo, NULL, &renderFinished);

    VkFenceCreateInfo fenceInfo;
    memset(&fenceInfo, 0, sizeof(fenceInfo));
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence inFlightFence = NULL;
    vkCreateFence(device, &fenceInfo, NULL, &inFlightFence);
    fprintf(stderr, "main: sync objects created\n"); fflush(stdout);

    /* ceil(NUM_PARTICLES / 64) workgroups -- matches the compute shader's
     * "local_size_x = 64" + bounds-check-against-200 design (see
     * particles_step.comp.spvasm's header comment). */
    const uint32_t computeGroups = (NUM_PARTICLES + 63) / 64;

    float dt = 1.0f / 60.0f;
    int frame_count = 0;
    const int max_frames = 300;
    fprintf(stderr, "main: entering render loop (%d frames)\n", max_frames); fflush(stdout);

    int running = 1;
    while (running && frame_count < max_frames) {
        running = platform_pump_events(&pw);
        if (!running) break;

        vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, ~0ull);
        vkResetFences(device, 1, &inFlightFence);

        uint32_t imageIndex = 0;
        vr = vkAcquireNextImageKHR(device, swapchain, ~0ull, imageAvailable, NULL, &imageIndex);
        if (vr != VK_SUCCESS && vr != VK_SUBOPTIMAL_KHR) {
            fprintf(stderr, "vkAcquireNextImageKHR failed vr=%d\n", (int)vr); fflush(stdout);
            break;
        }

        VkCommandBuffer cmd = cmdBufs[imageIndex];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo;
        memset(&beginInfo, 0, sizeof(beginInfo));
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        /* --- Vulkan compute: advance the physics simulation by one fixed
         * step, entirely on the GPU (writes land straight in the same
         * buffer the graphics pipeline below reads as its vertex buffer) --- */
        ComputeDispatchArgs dispatchArgs;
        dispatchArgs.cmd = cmd;
        dispatchArgs.computePipeline = computePipeline;
        dispatchArgs.computePipelineLayout = computePipelineLayout;
        dispatchArgs.descriptorSet = descriptorSet;
        dispatchArgs.vertexBuffer = vertexBuffer;
        dispatchArgs.vertexBufferSize = NUM_PARTICLES * sizeof(Vertex);
        dispatchArgs.velBuffer = velBuffer;
        dispatchArgs.velBufferSize = sizeof(hostVel);
        dispatchArgs.dt = dt;
        dispatchArgs.computeGroups = computeGroups;
        record_particle_compute_dispatch(&dispatchArgs);

        /* --- Vulkan: render this frame's particle positions --- */
        VkClearValue clearValue;
        memset(&clearValue, 0, sizeof(clearValue));
        clearValue.color.float32[0] = 0.04f;
        clearValue.color.float32[1] = 0.04f;
        clearValue.color.float32[2] = 0.08f;
        clearValue.color.float32[3] = 1.0f;

        VkRenderPassBeginInfo rpBegin;
        memset(&rpBegin, 0, sizeof(rpBegin));
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffers[imageIndex];
        rpBegin.renderArea.offset.x = 0; rpBegin.renderArea.offset.y = 0;
        rpBegin.renderArea.extent = extent;
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearValue;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkDeviceSize offset0 = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset0);
        vkCmdDraw(cmd, NUM_PARTICLES, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        VkFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo;
        memset(&submitInfo, 0, sizeof(submitInfo));
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished;
        vkQueueSubmit(queue, 1, &submitInfo, inFlightFence);

        VkPresentInfoKHR presentInfo;
        memset(&presentInfo, 0, sizeof(presentInfo));
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        vkQueuePresentKHR(queue, &presentInfo);

        frame_count++;
        if (frame_count % 60 == 0) {
            /* Read back just for progress logging -- waits for the queue
             * to drain first so this doesn't race the GPU's in-flight
             * writes; the render loop itself never depends on this. */
            vkQueueWaitIdle(queue);
            float dbg[2];
            void *dbgMap = NULL;
            vkMapMemory(device, vertexMemory, 0, sizeof(dbg), 0, &dbgMap);
            memcpy(dbg, dbgMap, sizeof(dbg));
            vkUnmapMemory(device, vertexMemory);
            fprintf(stderr, "main: frame=%d particle[0]=(%s,%s)\n", frame_count, platform_fmt3f((double)dbg[0]), platform_fmt3f((double)dbg[1])); fflush(stdout);
        }
    }

    fprintf(stderr, "main: render loop finished, frame_count=%d\n", frame_count); fflush(stdout);

    /* Same driver-cleanup-is-unreliable-after-real-work-is-done rationale
     * documented for the plain Vulkan triangle demo this session (see its
     * own longer comment): on Windows, DLL_PROCESS_DETACH hangs/crashes for
     * vulkan-1.dll; on Linux (this machine's software/llvmpipe driver),
     * even vkDeviceWaitIdle()/vkDestroyFence() alone crashed inside the
     * Vulkan ICD after a real, already-successful render loop. Skip ALL
     * Vulkan/window teardown and exit directly -- the OS reclaims every
     * resource on process exit regardless. */
    fprintf(stderr, "main: done, exiting cleanly\n"); fflush(stdout);
    platform_exit(0);
    return 0;
}
