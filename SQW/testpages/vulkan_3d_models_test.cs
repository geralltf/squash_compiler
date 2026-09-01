using System;
using System.Collections.Generic;

/* ===== 7b: leaf-first Vulkan struct mirrors ===== */

struct VkExtent2D {
    public uint width;
    public uint height;
    public IntPtr Ptr() { return this; }
}

struct VkOffset2D {
    public int x;
    public int y;
    public IntPtr Ptr() { return this; }
}

struct VkRect2D {
    public int offX;
    public int offY;
    public uint extW;
    public uint extH;
    public IntPtr Ptr() { return this; }
}

struct VkViewport {
    public float x;
    public float y;
    public float width;
    public float height;
    public float minDepth;
    public float maxDepth;
    public IntPtr Ptr() { return this; }
}

struct VkStencilOpState {
    public uint failOp;
    public uint passOp;
    public uint depthFailOp;
    public uint compareOp;
    public uint compareMask;
    public uint writeMask;
    public uint reference;
}

struct VkShaderModuleCreateInfo {
    public uint sType;
    public IntPtr pNext;
    public uint flags;
    public ulong codeSize;
    public IntPtr pCode;
    public IntPtr Ptr() { return this; }
}

struct VkPipelineShaderStageCreateInfo {
    public uint sType;
    public IntPtr pNext;
    public uint flags;
    public uint stage;
    public IntPtr module;
    public IntPtr pName;
    public IntPtr pSpecializationInfo;
    public IntPtr Ptr() { return this; }
}

struct VkVertexInputBindingDescription {
    public uint binding;
    public uint stride;
    public uint inputRate;
    public IntPtr Ptr() { return this; }
}

struct VkVertexInputAttributeDescription {
    public uint location;
    public uint binding;
    public uint format;
    public uint offset;
    public IntPtr Ptr() { return this; }
}

struct VkPipelineVertexInputStateCreateInfo {
    public uint sType;
    public IntPtr pNext;
    public uint flags;
    public uint vertexBindingDescriptionCount;
    public IntPtr pVertexBindingDescriptions;
    public uint vertexAttributeDescriptionCount;
    public IntPtr pVertexAttributeDescriptions;
    public IntPtr Ptr() { return this; }
}

struct VkPipelineInputAssemblyStateCreateInfo {
    public uint sType;
    public IntPtr pNext;
    public uint flags;
    public uint topology;
    public uint primitiveRestartEnable;
    public IntPtr Ptr() { return this; }
}

struct VkPipelineViewportStateCreateInfo {
    public uint sType;
    public IntPtr pNext;
    public uint flags;
    public uint viewportCount;
    public IntPtr pViewports;
    public uint scissorCount;
    public IntPtr pScissors;
    public IntPtr Ptr() { return this; }
}

struct VkPipelineRasterizationStateCreateInfo {
    public uint sType;
    public IntPtr pNext;
    public uint flags;
    public uint depthClampEnable;
    public uint rasterizerDiscardEnable;
    public uint polygonMode;
    public uint cullMode;
    public uint frontFace;
    public uint depthBiasEnable;
    public float depthBiasConstantFactor;
    public float depthBiasClamp;
    public float depthBiasSlopeFactor;
    public float lineWidth;
    public IntPtr Ptr() { return this; }
}

struct VkPipelineMultisampleStateCreateInfo {
    public uint sType;
    public IntPtr pNext;
    public uint flags;
    public uint rasterizationSamples;
    public uint sampleShadingEnable;
    public float minSampleShading;
    public IntPtr pSampleMask;
    public uint alphaToCoverageEnable;
    public uint alphaToOneEnable;
    public IntPtr Ptr() { return this; }
}

struct VkPipelineDepthStencilStateCreateInfo {
    public uint sType;
    public IntPtr pNext;
    public uint flags;
    public uint depthTestEnable;
    public uint depthWriteEnable;
    public uint depthCompareOp;
    public uint depthBoundsTestEnable;
    public uint stencilTestEnable;
    public VkStencilOpState front;
    public VkStencilOpState back;
    public float minDepthBounds;
    public float maxDepthBounds;
    public IntPtr Ptr() { return this; }
}

struct VkPipelineColorBlendAttachmentState {
    public uint blendEnable;
    public uint srcColorBlendFactor;
    public uint dstColorBlendFactor;
    public uint colorBlendOp;
    public uint srcAlphaBlendFactor;
    public uint dstAlphaBlendFactor;
    public uint alphaBlendOp;
    public uint colorWriteMask;
    public IntPtr Ptr() { return this; }
}

struct VkPipelineColorBlendStateCreateInfo {
    public uint sType;
    public IntPtr pNext;
    public uint flags;
    public uint logicOpEnable;
    public uint logicOp;
    public uint attachmentCount;
    public IntPtr pAttachments;
    public float blendConst0;
    public float blendConst1;
    public float blendConst2;
    public float blendConst3;
    public IntPtr Ptr() { return this; }
}

struct VkPushConstantRange {
    public uint stageFlags;
    public uint offset;
    public uint size;
    public IntPtr Ptr() { return this; }
}

struct VkPipelineLayoutCreateInfo {
    public uint sType;
    public IntPtr pNext;
    public uint flags;
    public uint setLayoutCount;
    public IntPtr pSetLayouts;
    public uint pushConstantRangeCount;
    public IntPtr pPushConstantRanges;
    public IntPtr Ptr() { return this; }
}

struct VkGraphicsPipelineCreateInfo {
    public uint sType;
    public IntPtr pNext;
    public uint flags;
    public uint stageCount;
    public IntPtr pStages;
    public IntPtr pVertexInputState;
    public IntPtr pInputAssemblyState;
    public IntPtr pTessellationState;
    public IntPtr pViewportState;
    public IntPtr pRasterizationState;
    public IntPtr pMultisampleState;
    public IntPtr pDepthStencilState;
    public IntPtr pColorBlendState;
    public IntPtr pDynamicState;
    public IntPtr layout;
    public IntPtr renderPass;
    public uint subpass;
    public IntPtr basePipelineHandle;
    public int basePipelineIndex;
    public IntPtr Ptr() { return this; }
}

struct VkBufferCreateInfo {
    public uint sType;
    public IntPtr pNext;
    public uint flags;
    public ulong size;
    public uint usage;
    public uint sharingMode;
    public uint queueFamilyIndexCount;
    public IntPtr pQueueFamilyIndices;
    public IntPtr Ptr() { return this; }
}

struct VkMemoryRequirements {
    public ulong size;
    public ulong alignment;
    public uint memoryTypeBits;
}

struct VkMemoryAllocateInfo {
    public uint sType;
    public IntPtr pNext;
    public ulong allocationSize;
    public uint memoryTypeIndex;
    public IntPtr Ptr() { return this; }
}

/* ===== Geometry types ===== */

struct Vertex {
    public float px;
    public float py;
    public float pz;
    public float nx;
    public float ny;
    public float nz;
    public float cr;
    public float cg;
    public float cb;
}

struct PointRY {
    public float r;
    public float y;
}

struct PointXYZ {
    public float x;
    public float y;
    public float z;
}

/* ===== Native declarations ===== */

class Native {
    [DllImport("csharp_rt")] public static extern IntPtr csr_list_data(IntPtr list);
    [DllImport("csharp_rt")] public static extern IntPtr csr_string_data(string s);

    [DllImport("c")] public static extern void memcpy(IntPtr dest, IntPtr src, ulong n);
    [DllImport("m")] public static extern float sinf(float x);
    [DllImport("m")] public static extern float cosf(float x);
    [DllImport("m")] public static extern float sqrtf(float x);
    [DllImport("m")] public static extern float tanf(float x);

    [DllImport("sqw")] public static extern IntPtr SqwGetDevice();
    [DllImport("sqw")] public static extern IntPtr SqwGetRenderPass();
    [DllImport("sqw")] public static extern void SqwGetExtent(ref uint outW, ref uint outH);
    [DllImport("sqw")] public static extern int SqwFindMemoryType(uint typeBits, uint properties);
    [DllImport("sqw")] public static extern void SqwRegisterRenderCallback(IntPtr fn);
    [DllImport("sqw")] public static extern void SqwRegisterClickHandler(string elementId, IntPtr fn);
    [DllImport("sqw")] public static extern void SqwSetElementText(string elementId, string text);

    [DllImport("vulkan")] public static extern int vkCreateShaderModule(IntPtr device, ref VkShaderModuleCreateInfo pCreateInfo, IntPtr pAllocator, out IntPtr pShaderModule);
    [DllImport("vulkan")] public static extern int vkCreatePipelineLayout(IntPtr device, ref VkPipelineLayoutCreateInfo pCreateInfo, IntPtr pAllocator, out IntPtr pPipelineLayout);
    [DllImport("vulkan")] public static extern int vkCreateGraphicsPipelines(IntPtr device, IntPtr pipelineCache, uint createInfoCount, ref VkGraphicsPipelineCreateInfo pCreateInfos, IntPtr pAllocator, out IntPtr pPipelines);
    [DllImport("vulkan")] public static extern int vkCreateBuffer(IntPtr device, ref VkBufferCreateInfo pCreateInfo, IntPtr pAllocator, out IntPtr pBuffer);
    [DllImport("vulkan")] public static extern void vkGetBufferMemoryRequirements(IntPtr device, IntPtr buffer, out VkMemoryRequirements pMemoryRequirements);
    [DllImport("vulkan")] public static extern int vkAllocateMemory(IntPtr device, ref VkMemoryAllocateInfo pAllocateInfo, IntPtr pAllocator, out IntPtr pMemory);
    [DllImport("vulkan")] public static extern int vkBindBufferMemory(IntPtr device, IntPtr buffer, IntPtr memory, ulong memoryOffset);
    [DllImport("vulkan")] public static extern int vkMapMemory(IntPtr device, IntPtr memory, ulong offset, ulong size, uint flags, out IntPtr ppData);
    [DllImport("vulkan")] public static extern void vkUnmapMemory(IntPtr device, IntPtr memory);
    [DllImport("vulkan")] public static extern void vkCmdBindPipeline(IntPtr commandBuffer, uint pipelineBindPoint, IntPtr pipeline);
    [DllImport("vulkan")] public static extern void vkCmdBindVertexBuffers(IntPtr commandBuffer, uint firstBinding, uint bindingCount, ref IntPtr pBuffers, ref ulong pOffsets);
    [DllImport("vulkan")] public static extern void vkCmdBindIndexBuffer(IntPtr commandBuffer, IntPtr buffer, ulong offset, uint indexType);
    [DllImport("vulkan")] public static extern void vkCmdDrawIndexed(IntPtr commandBuffer, uint indexCount, uint instanceCount, uint firstIndex, int vertexOffset, uint firstInstance);
    [DllImport("vulkan")] public static extern void vkCmdPushConstants(IntPtr commandBuffer, IntPtr layout, uint stageFlags, uint offset, uint size, IntPtr pValues);
}

/* ===== Matrix / geometry helpers ===== */

class MathHelp {
    public static List<float> Identity() {
        List<float> m = new List<float>();
        int i = 0;
        while (i < 16) { m.Add(0.0f); i = i + 1; }
        m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f; m[15] = 1.0f;
        return m;
    }

    public static List<float> Mul(List<float> a, List<float> b) {
        List<float> r = new List<float>();
        int i = 0;
        while (i < 16) { r.Add(0.0f); i = i + 1; }
        int c = 0;
        while (c < 4) {
            int row = 0;
            while (row < 4) {
                float sum = 0.0f;
                int k = 0;
                while (k < 4) {
                    sum = sum + a[k * 4 + row] * b[c * 4 + k];
                    k = k + 1;
                }
                r[c * 4 + row] = sum;
                row = row + 1;
            }
            c = c + 1;
        }
        return r;
    }

    public static List<float> Perspective(float fovYRad, float aspect, float znear, float zfar) {
        List<float> m = new List<float>();
        int i = 0;
        while (i < 16) { m.Add(0.0f); i = i + 1; }
        float f = 1.0f / Native.tanf(fovYRad * 0.5f);
        m[0] = f / aspect;
        m[5] = -f;
        m[10] = zfar / (znear - zfar);
        m[11] = -1.0f;
        m[14] = (zfar * znear) / (znear - zfar);
        return m;
    }

    public static List<float> Translate(float tx, float ty, float tz) {
        List<float> m = Identity();
        m[12] = tx; m[13] = ty; m[14] = tz;
        return m;
    }

    public static List<float> RotateY(float a) {
        List<float> m = Identity();
        float s = Native.sinf(a);
        float c = Native.cosf(a);
        m[0] = c; m[2] = s;
        m[8] = -s; m[10] = c;
        return m;
    }

    public static List<float> RotateX(float a) {
        List<float> m = Identity();
        float s = Native.sinf(a);
        float c = Native.cosf(a);
        m[5] = c; m[6] = s;
        m[9] = -s; m[10] = c;
        return m;
    }
}

class Geom {
    static void AddVert(List<Vertex> outV, float px, float py, float pz, float nx, float ny, float nz, float r, float g, float b) {
        Vertex v = new Vertex();
        v.px = px; v.py = py; v.pz = pz;
        v.nx = nx; v.ny = ny; v.nz = nz;
        v.cr = r; v.cg = g; v.cb = b;
        outV.Add(v);
    }

    public static void AddQuad(List<Vertex> outV, List<ushort> outI,
        float x0, float y0, float z0, float x1, float y1, float z1,
        float x2, float y2, float z2, float x3, float y3, float z3,
        float nx, float ny, float nz, float r, float g, float b) {
        ushort baseIdx = (ushort)outV.Count;
        AddVert(outV, x0, y0, z0, nx, ny, nz, r, g, b);
        AddVert(outV, x1, y1, z1, nx, ny, nz, r, g, b);
        AddVert(outV, x2, y2, z2, nx, ny, nz, r, g, b);
        AddVert(outV, x3, y3, z3, nx, ny, nz, r, g, b);
        outI.Add(baseIdx); outI.Add((ushort)(baseIdx + 1)); outI.Add((ushort)(baseIdx + 2));
        outI.Add(baseIdx); outI.Add((ushort)(baseIdx + 2)); outI.Add((ushort)(baseIdx + 3));
    }

    public static void BuildRevolve(List<PointRY> profile, int nSeg, float r, float g, float b, List<Vertex> outV, List<ushort> outI) {
        int nProf = profile.Count;
        int ring = 0;
        while (ring <= nSeg) {
            float ang = 6.2831853f * (float)ring / (float)nSeg;
            float ca = Native.cosf(ang);
            float sa = Native.sinf(ang);
            int p = 0;
            while (p < nProf) {
                PointRY curPt = profile[p];
                float prad = curPt.r;
                float py = curPt.y;
                float px = prad * ca;
                float pz = prad * sa;
                float slopeR = 0.0f;
                float slopeY = 1.0f;
                if (p < nProf - 1) {
                    PointRY nextPt = profile[p + 1];
                    slopeR = nextPt.r - curPt.r;
                    slopeY = nextPt.y - curPt.y;
                } else if (p > 0) {
                    PointRY prevPt = profile[p - 1];
                    slopeR = curPt.r - prevPt.r;
                    slopeY = curPt.y - prevPt.y;
                }
                float tanx = slopeR * ca;
                float tanz = slopeR * sa;
                float tany = slopeY;
                float bx = -sa;
                float bz = ca;
                float nx = tany * bz - 0.0f * bz;
                nx = (tanz * 0.0f) - (tany * bz);
                float nz = (tany * bx) - (tanx * 0.0f);
                float ny2 = (tanx * bz) - (tanz * bx);
                float len = Native.sqrtf(nx * nx + ny2 * ny2 + nz * nz);
                if (len < 0.0001f) { nx = ca; ny2 = 0.0f; nz = sa; len = 1.0f; }
                nx = nx / len; ny2 = ny2 / len; nz = nz / len;
                AddVert(outV, px, py, pz, nx, ny2, nz, r, g, b);
                p = p + 1;
            }
            ring = ring + 1;
        }
        int seg = 0;
        while (seg < nSeg) {
            int p2 = 0;
            while (p2 < nProf - 1) {
                ushort i0 = (ushort)(seg * nProf + p2);
                ushort i1 = (ushort)(seg * nProf + p2 + 1);
                ushort i2 = (ushort)((seg + 1) * nProf + p2 + 1);
                ushort i3 = (ushort)((seg + 1) * nProf + p2);
                outI.Add(i0); outI.Add(i1); outI.Add(i2);
                outI.Add(i0); outI.Add(i2); outI.Add(i3);
                p2 = p2 + 1;
            }
            seg = seg + 1;
        }
    }

    public static void BuildTube(List<PointXYZ> path, List<float> taper, int ringSeg, float r, float g, float b, List<Vertex> outV, List<ushort> outI) {
        int nPath = path.Count;
        int i = 0;
        while (i < nPath) {
            float tx;
            float ty;
            float tz;
            PointXYZ curPathPt = path[i];
            if (i < nPath - 1) {
                PointXYZ nextPathPt = path[i + 1];
                tx = nextPathPt.x - curPathPt.x;
                ty = nextPathPt.y - curPathPt.y;
                tz = nextPathPt.z - curPathPt.z;
            } else {
                PointXYZ prevPathPt = path[i - 1];
                tx = curPathPt.x - prevPathPt.x;
                ty = curPathPt.y - prevPathPt.y;
                tz = curPathPt.z - prevPathPt.z;
            }
            float tl = Native.sqrtf(tx * tx + ty * ty + tz * tz);
            if (tl < 0.0001f) tl = 1.0f;
            tx = tx / tl; ty = ty / tl; tz = tz / tl;
            float upx = 0.0f;
            float upy = 1.0f;
            float upz = 0.0f;
            float dotv = tx * upx + ty * upy + tz * upz;
            if (dotv > 0.99f || dotv < -0.99f) { upx = 1.0f; upy = 0.0f; upz = 0.0f; }
            float p1x = ty * upz - tz * upy;
            float p1y = tz * upx - tx * upz;
            float p1z = tx * upy - ty * upx;
            float p1l = Native.sqrtf(p1x * p1x + p1y * p1y + p1z * p1z);
            p1x = p1x / p1l; p1y = p1y / p1l; p1z = p1z / p1l;
            float p2x = ty * p1z - tz * p1y;
            float p2y = tz * p1x - tx * p1z;
            float p2z = tx * p1y - ty * p1x;
            float rad = taper[i];
            int s = 0;
            while (s <= ringSeg) {
                float ang = 6.2831853f * (float)s / (float)ringSeg;
                float ca = Native.cosf(ang);
                float sa = Native.sinf(ang);
                float ox = p1x * ca + p2x * sa;
                float oy = p1y * ca + p2y * sa;
                float oz = p1z * ca + p2z * sa;
                float vx = curPathPt.x + ox * rad;
                float vy = curPathPt.y + oy * rad;
                float vz = curPathPt.z + oz * rad;
                AddVert(outV, vx, vy, vz, ox, oy, oz, r, g, b);
                s = s + 1;
            }
            i = i + 1;
        }
        int ring = 0;
        int ringStride = ringSeg + 1;
        while (ring < nPath - 1) {
            int k = 0;
            while (k < ringSeg) {
                ushort i0 = (ushort)(ring * ringStride + k);
                ushort i1 = (ushort)(ring * ringStride + k + 1);
                ushort i2 = (ushort)((ring + 1) * ringStride + k + 1);
                ushort i3 = (ushort)((ring + 1) * ringStride + k);
                outI.Add(i0); outI.Add(i1); outI.Add(i2);
                outI.Add(i0); outI.Add(i2); outI.Add(i3);
                k = k + 1;
            }
            ring = ring + 1;
        }
    }
}

class Models {
    public static void BuildTeapot(List<Vertex> outV, List<ushort> outI) {
        List<PointRY> body = new List<PointRY>();
        PointRY p;
        p = new PointRY(); p.r = 0.02f; p.y = -0.95f; body.Add(p);
        p = new PointRY(); p.r = 0.55f; p.y = -0.85f; body.Add(p);
        p = new PointRY(); p.r = 0.82f; p.y = -0.55f; body.Add(p);
        p = new PointRY(); p.r = 0.85f; p.y = -0.15f; body.Add(p);
        p = new PointRY(); p.r = 0.80f; p.y = 0.15f; body.Add(p);
        p = new PointRY(); p.r = 0.62f; p.y = 0.42f; body.Add(p);
        p = new PointRY(); p.r = 0.42f; p.y = 0.55f; body.Add(p);
        p = new PointRY(); p.r = 0.30f; p.y = 0.58f; body.Add(p);
        p = new PointRY(); p.r = 0.30f; p.y = 0.63f; body.Add(p);
        p = new PointRY(); p.r = 0.20f; p.y = 0.66f; body.Add(p);
        p = new PointRY(); p.r = 0.02f; p.y = 0.68f; body.Add(p);
        Geom.BuildRevolve(body, 24, 0.85f, 0.65f, 0.25f, outV, outI);

        List<PointRY> lid = new List<PointRY>();
        p = new PointRY(); p.r = 0.02f; p.y = 0.68f; lid.Add(p);
        p = new PointRY(); p.r = 0.14f; p.y = 0.70f; lid.Add(p);
        p = new PointRY(); p.r = 0.14f; p.y = 0.78f; lid.Add(p);
        p = new PointRY(); p.r = 0.02f; p.y = 0.82f; lid.Add(p);
        Geom.BuildRevolve(lid, 16, 0.9f, 0.7f, 0.3f, outV, outI);

        List<PointXYZ> spoutPath = new List<PointXYZ>();
        PointXYZ pt;
        pt = new PointXYZ(); pt.x = 0.78f; pt.y = -0.05f; pt.z = 0.0f; spoutPath.Add(pt);
        pt = new PointXYZ(); pt.x = 1.05f; pt.y = 0.10f; pt.z = 0.0f; spoutPath.Add(pt);
        pt = new PointXYZ(); pt.x = 1.28f; pt.y = 0.30f; pt.z = 0.0f; spoutPath.Add(pt);
        pt = new PointXYZ(); pt.x = 1.45f; pt.y = 0.52f; pt.z = 0.0f; spoutPath.Add(pt);
        pt = new PointXYZ(); pt.x = 1.52f; pt.y = 0.66f; pt.z = 0.0f; spoutPath.Add(pt);
        pt = new PointXYZ(); pt.x = 1.50f; pt.y = 0.72f; pt.z = 0.0f; spoutPath.Add(pt);
        List<float> spoutTaper = new List<float>();
        spoutTaper.Add(0.22f); spoutTaper.Add(0.19f); spoutTaper.Add(0.15f);
        spoutTaper.Add(0.11f); spoutTaper.Add(0.07f); spoutTaper.Add(0.05f);
        Geom.BuildTube(spoutPath, spoutTaper, 10, 0.85f, 0.65f, 0.25f, outV, outI);

        List<PointXYZ> handlePath = new List<PointXYZ>();
        List<float> handleTaper = new List<float>();
        float cx = -0.75f;
        float cy = 0.05f;
        float hr = 0.45f;
        int nH = 9;
        int i = 0;
        while (i < nH) {
            float t = 3.14159265f * (float)i / (float)(nH - 1);
            float hx = cx - hr * Native.cosf(t);
            float hy = cy + hr * Native.sinf(t);
            pt = new PointXYZ(); pt.x = hx; pt.y = hy; pt.z = 0.0f;
            handlePath.Add(pt);
            handleTaper.Add(0.075f);
            i = i + 1;
        }
        PointXYZ pend;
        pend = new PointXYZ(); pend.x = -0.70f; pend.y = 0.40f; pend.z = 0.0f;
        handlePath[0] = pend;
        pend = new PointXYZ(); pend.x = -0.70f; pend.y = -0.35f; pend.z = 0.0f;
        handlePath[nH - 1] = pend;
        Geom.BuildTube(handlePath, handleTaper, 8, 0.85f, 0.65f, 0.25f, outV, outI);
    }

    public static void BuildCube(List<Vertex> outV, List<ushort> outI) {
        Geom.AddQuad(outV, outI, -1,-1,-1, 1,-1,-1, 1,1,-1, -1,1,-1, 0,0,-1, 1.0f,0.3f,0.3f);
        Geom.AddQuad(outV, outI, 1,-1,1, -1,-1,1, -1,1,1, 1,1,1, 0,0,1, 0.3f,1.0f,0.3f);
        Geom.AddQuad(outV, outI, -1,-1,1, -1,-1,-1, -1,1,-1, -1,1,1, -1,0,0, 0.3f,0.3f,1.0f);
        Geom.AddQuad(outV, outI, 1,-1,-1, 1,-1,1, 1,1,1, 1,1,-1, 1,0,0, 1.0f,1.0f,0.3f);
        Geom.AddQuad(outV, outI, -1,1,-1, 1,1,-1, 1,1,1, -1,1,1, 0,1,0, 1.0f,0.3f,1.0f);
        Geom.AddQuad(outV, outI, -1,-1,1, 1,-1,1, 1,-1,-1, -1,-1,-1, 0,-1,0, 0.3f,1.0f,1.0f);
    }

    public static void BuildSphere(List<Vertex> outV, List<ushort> outI) {
        List<PointRY> profile = new List<PointRY>();
        int nStep = 16;
        int i = 0;
        while (i <= nStep) {
            float t = 3.14159265f * (float)i / (float)nStep;
            PointRY p = new PointRY();
            p.r = Native.sinf(t);
            p.y = -Native.cosf(t);
            profile.Add(p);
            i = i + 1;
        }
        Geom.BuildRevolve(profile, 24, 0.3f, 0.6f, 0.95f, outV, outI);
    }
}

/* ===== Embedded, hand-authored, spirv-val-validated SPIR-V shaders ===== */

class Shaders {
    public static List<uint> LoadVertSpirv() {
        List<uint> w = new List<uint>();
        w.Add(119734787u); w.Add(65536u); w.Add(524299u); w.Add(42u); w.Add(0u); w.Add(131089u); w.Add(1u); w.Add(393227u);
        w.Add(1u); w.Add(1280527431u); w.Add(1685353262u); w.Add(808793134u); w.Add(0u); w.Add(196622u); w.Add(0u); w.Add(1u);
        w.Add(720911u); w.Add(0u); w.Add(4u); w.Add(1852399981u); w.Add(0u); w.Add(13u); w.Add(25u); w.Add(36u);
        w.Add(37u); w.Add(39u); w.Add(40u); w.Add(196611u); w.Add(2u); w.Add(450u); w.Add(262149u); w.Add(4u);
        w.Add(1852399981u); w.Add(0u); w.Add(393221u); w.Add(11u); w.Add(1348430951u); w.Add(1700164197u); w.Add(2019914866u); w.Add(0u);
        w.Add(393222u); w.Add(11u); w.Add(0u); w.Add(1348430951u); w.Add(1953067887u); w.Add(7237481u); w.Add(458758u); w.Add(11u);
        w.Add(1u); w.Add(1348430951u); w.Add(1953393007u); w.Add(1702521171u); w.Add(0u); w.Add(458758u); w.Add(11u); w.Add(2u);
        w.Add(1130327143u); w.Add(1148217708u); w.Add(1635021673u); w.Add(6644590u); w.Add(458758u); w.Add(11u); w.Add(3u); w.Add(1130327143u);
        w.Add(1147956341u); w.Add(1635021673u); w.Add(6644590u); w.Add(196613u); w.Add(13u); w.Add(0u); w.Add(196613u); w.Add(17u);
        w.Add(17232u); w.Add(262150u); w.Add(17u); w.Add(0u); w.Add(7370349u); w.Add(196613u); w.Add(19u); w.Add(25456u);
        w.Add(262149u); w.Add(25u); w.Add(1867542121u); w.Add(115u); w.Add(327685u); w.Add(36u); w.Add(1316255087u); w.Add(1634562671u);
        w.Add(108u); w.Add(327685u); w.Add(37u); w.Add(1867411049u); w.Add(1818324338u); w.Add(0u); w.Add(327685u); w.Add(39u);
        w.Add(1131705711u); w.Add(1919904879u); w.Add(0u); w.Add(262149u); w.Add(40u); w.Add(1866690153u); w.Add(7499628u); w.Add(196679u);
        w.Add(11u); w.Add(2u); w.Add(327752u); w.Add(11u); w.Add(0u); w.Add(11u); w.Add(0u); w.Add(327752u);
        w.Add(11u); w.Add(1u); w.Add(11u); w.Add(1u); w.Add(327752u); w.Add(11u); w.Add(2u); w.Add(11u);
        w.Add(3u); w.Add(327752u); w.Add(11u); w.Add(3u); w.Add(11u); w.Add(4u); w.Add(196679u); w.Add(17u);
        w.Add(2u); w.Add(262216u); w.Add(17u); w.Add(0u); w.Add(5u); w.Add(327752u); w.Add(17u); w.Add(0u);
        w.Add(7u); w.Add(16u); w.Add(327752u); w.Add(17u); w.Add(0u); w.Add(35u); w.Add(0u); w.Add(262215u);
        w.Add(25u); w.Add(30u); w.Add(0u); w.Add(262215u); w.Add(36u); w.Add(30u); w.Add(0u); w.Add(262215u);
        w.Add(37u); w.Add(30u); w.Add(1u); w.Add(262215u); w.Add(39u); w.Add(30u); w.Add(1u); w.Add(262215u);
        w.Add(40u); w.Add(30u); w.Add(2u); w.Add(131091u); w.Add(2u); w.Add(196641u); w.Add(3u); w.Add(2u);
        w.Add(196630u); w.Add(6u); w.Add(32u); w.Add(262167u); w.Add(7u); w.Add(6u); w.Add(4u); w.Add(262165u);
        w.Add(8u); w.Add(32u); w.Add(0u); w.Add(262187u); w.Add(8u); w.Add(9u); w.Add(1u); w.Add(262172u);
        w.Add(10u); w.Add(6u); w.Add(9u); w.Add(393246u); w.Add(11u); w.Add(7u); w.Add(6u); w.Add(10u);
        w.Add(10u); w.Add(262176u); w.Add(12u); w.Add(3u); w.Add(11u); w.Add(262203u); w.Add(12u); w.Add(13u);
        w.Add(3u); w.Add(262165u); w.Add(14u); w.Add(32u); w.Add(1u); w.Add(262187u); w.Add(14u); w.Add(15u);
        w.Add(0u); w.Add(262168u); w.Add(16u); w.Add(7u); w.Add(4u); w.Add(196638u); w.Add(17u); w.Add(16u);
        w.Add(262176u); w.Add(18u); w.Add(9u); w.Add(17u); w.Add(262203u); w.Add(18u); w.Add(19u); w.Add(9u);
        w.Add(262176u); w.Add(20u); w.Add(9u); w.Add(16u); w.Add(262167u); w.Add(23u); w.Add(6u); w.Add(3u);
        w.Add(262176u); w.Add(24u); w.Add(1u); w.Add(23u); w.Add(262203u); w.Add(24u); w.Add(25u); w.Add(1u);
        w.Add(262187u); w.Add(6u); w.Add(27u); w.Add(1065353216u); w.Add(262176u); w.Add(33u); w.Add(3u); w.Add(7u);
        w.Add(262176u); w.Add(35u); w.Add(3u); w.Add(23u); w.Add(262203u); w.Add(35u); w.Add(36u); w.Add(3u);
        w.Add(262203u); w.Add(24u); w.Add(37u); w.Add(1u); w.Add(262203u); w.Add(35u); w.Add(39u); w.Add(3u);
        w.Add(262203u); w.Add(24u); w.Add(40u); w.Add(1u); w.Add(327734u); w.Add(2u); w.Add(4u); w.Add(0u);
        w.Add(3u); w.Add(131320u); w.Add(5u); w.Add(327745u); w.Add(20u); w.Add(21u); w.Add(19u); w.Add(15u);
        w.Add(262205u); w.Add(16u); w.Add(22u); w.Add(21u); w.Add(262205u); w.Add(23u); w.Add(26u); w.Add(25u);
        w.Add(327761u); w.Add(6u); w.Add(28u); w.Add(26u); w.Add(0u); w.Add(327761u); w.Add(6u); w.Add(29u);
        w.Add(26u); w.Add(1u); w.Add(327761u); w.Add(6u); w.Add(30u); w.Add(26u); w.Add(2u); w.Add(458832u);
        w.Add(7u); w.Add(31u); w.Add(28u); w.Add(29u); w.Add(30u); w.Add(27u); w.Add(327825u); w.Add(7u);
        w.Add(32u); w.Add(22u); w.Add(31u); w.Add(327745u); w.Add(33u); w.Add(34u); w.Add(13u); w.Add(15u);
        w.Add(196670u); w.Add(34u); w.Add(32u); w.Add(262205u); w.Add(23u); w.Add(38u); w.Add(37u); w.Add(196670u);
        w.Add(36u); w.Add(38u); w.Add(262205u); w.Add(23u); w.Add(41u); w.Add(40u); w.Add(196670u); w.Add(39u);
        w.Add(41u); w.Add(65789u); w.Add(65592u);
        return w;
    }

    public static List<uint> LoadFragSpirv() {
        List<uint> w = new List<uint>();
        w.Add(119734787u); w.Add(65536u); w.Add(524299u); w.Add(38u); w.Add(0u); w.Add(131089u); w.Add(1u); w.Add(393227u);
        w.Add(1u); w.Add(1280527431u); w.Add(1685353262u); w.Add(808793134u); w.Add(0u); w.Add(196622u); w.Add(0u); w.Add(1u);
        w.Add(524303u); w.Add(4u); w.Add(4u); w.Add(1852399981u); w.Add(0u); w.Add(11u); w.Add(28u); w.Add(29u);
        w.Add(196624u); w.Add(4u); w.Add(7u); w.Add(196611u); w.Add(2u); w.Add(450u); w.Add(262149u); w.Add(4u);
        w.Add(1852399981u); w.Add(0u); w.Add(196613u); w.Add(9u); w.Add(110u); w.Add(327685u); w.Add(11u); w.Add(1867411049u);
        w.Add(1818324338u); w.Add(0u); w.Add(327685u); w.Add(14u); w.Add(1751607660u); w.Add(1919501428u); w.Add(0u); w.Add(262149u);
        w.Add(20u); w.Add(1717987684u); w.Add(0u); w.Add(327685u); w.Add(28u); w.Add(1131705711u); w.Add(1919904879u); w.Add(0u);
        w.Add(262149u); w.Add(29u); w.Add(1866690153u); w.Add(7499628u); w.Add(262215u); w.Add(11u); w.Add(30u); w.Add(0u);
        w.Add(262215u); w.Add(28u); w.Add(30u); w.Add(0u); w.Add(262215u); w.Add(29u); w.Add(30u); w.Add(1u);
        w.Add(131091u); w.Add(2u); w.Add(196641u); w.Add(3u); w.Add(2u); w.Add(196630u); w.Add(6u); w.Add(32u);
        w.Add(262167u); w.Add(7u); w.Add(6u); w.Add(3u); w.Add(262176u); w.Add(8u); w.Add(7u); w.Add(7u);
        w.Add(262176u); w.Add(10u); w.Add(1u); w.Add(7u); w.Add(262203u); w.Add(10u); w.Add(11u); w.Add(1u);
        w.Add(262187u); w.Add(6u); w.Add(15u); w.Add(1054680699u); w.Add(262187u); w.Add(6u); w.Add(16u); w.Add(1063069307u);
        w.Add(262187u); w.Add(6u); w.Add(17u); w.Add(1048883376u); w.Add(393260u); w.Add(7u); w.Add(18u); w.Add(15u);
        w.Add(16u); w.Add(17u); w.Add(262176u); w.Add(19u); w.Add(7u); w.Add(6u); w.Add(262187u); w.Add(6u);
        w.Add(24u); w.Add(1041865114u); w.Add(262167u); w.Add(26u); w.Add(6u); w.Add(4u); w.Add(262176u); w.Add(27u);
        w.Add(3u); w.Add(26u); w.Add(262203u); w.Add(27u); w.Add(28u); w.Add(3u); w.Add(262203u); w.Add(10u);
        w.Add(29u); w.Add(1u); w.Add(262187u); w.Add(6u); w.Add(33u); w.Add(1065353216u); w.Add(327734u); w.Add(2u);
        w.Add(4u); w.Add(0u); w.Add(3u); w.Add(131320u); w.Add(5u); w.Add(262203u); w.Add(8u); w.Add(9u);
        w.Add(7u); w.Add(262203u); w.Add(8u); w.Add(14u); w.Add(7u); w.Add(262203u); w.Add(19u); w.Add(20u);
        w.Add(7u); w.Add(262205u); w.Add(7u); w.Add(12u); w.Add(11u); w.Add(393228u); w.Add(7u); w.Add(13u);
        w.Add(1u); w.Add(69u); w.Add(12u); w.Add(196670u); w.Add(9u); w.Add(13u); w.Add(196670u); w.Add(14u);
        w.Add(18u); w.Add(262205u); w.Add(7u); w.Add(21u); w.Add(9u); w.Add(262205u); w.Add(7u); w.Add(22u);
        w.Add(14u); w.Add(327828u); w.Add(6u); w.Add(23u); w.Add(21u); w.Add(22u); w.Add(458764u); w.Add(6u);
        w.Add(25u); w.Add(1u); w.Add(40u); w.Add(23u); w.Add(24u); w.Add(196670u); w.Add(20u); w.Add(25u);
        w.Add(262205u); w.Add(7u); w.Add(30u); w.Add(29u); w.Add(262205u); w.Add(6u); w.Add(31u); w.Add(20u);
        w.Add(327822u); w.Add(7u); w.Add(32u); w.Add(30u); w.Add(31u); w.Add(327761u); w.Add(6u); w.Add(34u);
        w.Add(32u); w.Add(0u); w.Add(327761u); w.Add(6u); w.Add(35u); w.Add(32u); w.Add(1u); w.Add(327761u);
        w.Add(6u); w.Add(36u); w.Add(32u); w.Add(2u); w.Add(458832u); w.Add(26u); w.Add(37u); w.Add(34u);
        w.Add(35u); w.Add(36u); w.Add(33u); w.Add(196670u); w.Add(28u); w.Add(37u); w.Add(65789u); w.Add(65592u);
        return w;
    }

    public static IntPtr CreateShaderModule(IntPtr device, List<uint> words) {
        VkShaderModuleCreateInfo ci = new VkShaderModuleCreateInfo();
        ci.sType = 16u;
        ci.codeSize = (ulong)(words.Count * 4);
        ci.pCode = Native.csr_list_data(words);
        IntPtr module;
        Native.vkCreateShaderModule(device, ref ci, 0, out module);
        return module;
    }
}

/* ===== GPU mesh + main program ===== */

class Program {
    static IntPtr device;
    static IntPtr renderPass;
    static IntPtr pipelineLayout;
    static IntPtr pipeline;

    /* Three parallel lists rather than a List<GpuMesh> (a struct mixing
     * IntPtr and uint fields) -- a real squash codegen bug (found while
     * debugging why the page hung SQW's render loop): reading a mixed-
     * pointer+int struct back out of a List<T> corrupted its second
     * field. Parallel lists of a single scalar type each sidestep it
     * entirely. */
    static List<IntPtr> meshVBufs = new List<IntPtr>();
    static List<IntPtr> meshIBufs = new List<IntPtr>();
    static List<uint> meshICounts = new List<uint>();
    static int currentModel = 0;
    static float angle = 0.0f;

    static IntPtr CreateBuffer(ulong byteSize, uint usage, IntPtr srcData) {
        VkBufferCreateInfo bci = new VkBufferCreateInfo();
        bci.sType = 12u;
        bci.size = byteSize;
        bci.usage = usage;
        bci.sharingMode = 0u;
        IntPtr buf;
        Native.vkCreateBuffer(device, ref bci, 0, out buf);

        VkMemoryRequirements req;
        Native.vkGetBufferMemoryRequirements(device, buf, out req);
        int memType = Native.SqwFindMemoryType(req.memoryTypeBits, 6u);

        VkMemoryAllocateInfo mai = new VkMemoryAllocateInfo();
        mai.sType = 5u;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = (uint)memType;
        IntPtr mem;
        Native.vkAllocateMemory(device, ref mai, 0, out mem);
        Native.vkBindBufferMemory(device, buf, mem, 0UL);

        IntPtr mapped;
        Native.vkMapMemory(device, mem, 0UL, byteSize, 0u, out mapped);
        Native.memcpy(mapped, srcData, byteSize);
        Native.vkUnmapMemory(device, mem);
        return buf;
    }

    static void UploadMesh(List<Vertex> verts, List<ushort> indices) {
        ulong vbytes = (ulong)(verts.Count * 36);
        ulong ibytes = (ulong)(indices.Count * 2);
        IntPtr vbuf = CreateBuffer(vbytes, 128u, Native.csr_list_data(verts));
        IntPtr ibuf = CreateBuffer(ibytes, 64u, Native.csr_list_data(indices));
        meshVBufs.Add(vbuf);
        meshIBufs.Add(ibuf);
        meshICounts.Add((uint)indices.Count);
    }

    static string ModelName(int idx) {
        if (idx == 0) return "Teapot -- a lathed body with a swept spout and handle";
        if (idx == 1) return "Cube -- a procedural six-sided box";
        return "Sphere -- a procedural lathed UV sphere";
    }

    public static void Main() {
        device = Native.SqwGetDevice();
        renderPass = Native.SqwGetRenderPass();

        List<Vertex> tv = new List<Vertex>();
        List<ushort> ti = new List<ushort>();
        Models.BuildTeapot(tv, ti);
        UploadMesh(tv, ti);

        List<Vertex> cv = new List<Vertex>();
        List<ushort> ci = new List<ushort>();
        Models.BuildCube(cv, ci);
        UploadMesh(cv, ci);

        List<Vertex> sv = new List<Vertex>();
        List<ushort> si = new List<ushort>();
        Models.BuildSphere(sv, si);
        UploadMesh(sv, si);

        IntPtr vertMod = Shaders.CreateShaderModule(device, Shaders.LoadVertSpirv());
        IntPtr fragMod = Shaders.CreateShaderModule(device, Shaders.LoadFragSpirv());

        List<VkPipelineShaderStageCreateInfo> stages = new List<VkPipelineShaderStageCreateInfo>();
        VkPipelineShaderStageCreateInfo vs = new VkPipelineShaderStageCreateInfo();
        vs.sType = 18u;
        vs.stage = 1u;
        vs.module = vertMod;
        vs.pName = Native.csr_string_data("main");
        stages.Add(vs);
        VkPipelineShaderStageCreateInfo fs = new VkPipelineShaderStageCreateInfo();
        fs.sType = 18u;
        fs.stage = 16u;
        fs.module = fragMod;
        fs.pName = Native.csr_string_data("main");
        stages.Add(fs);

        List<VkVertexInputBindingDescription> bindings = new List<VkVertexInputBindingDescription>();
        VkVertexInputBindingDescription bd = new VkVertexInputBindingDescription();
        bd.binding = 0u;
        bd.stride = 36u;
        bd.inputRate = 0u;
        bindings.Add(bd);

        List<VkVertexInputAttributeDescription> attrs = new List<VkVertexInputAttributeDescription>();
        VkVertexInputAttributeDescription a0 = new VkVertexInputAttributeDescription();
        a0.location = 0u; a0.binding = 0u; a0.format = 106u; a0.offset = 0u;
        attrs.Add(a0);
        VkVertexInputAttributeDescription a1 = new VkVertexInputAttributeDescription();
        a1.location = 1u; a1.binding = 0u; a1.format = 106u; a1.offset = 12u;
        attrs.Add(a1);
        VkVertexInputAttributeDescription a2 = new VkVertexInputAttributeDescription();
        a2.location = 2u; a2.binding = 0u; a2.format = 106u; a2.offset = 24u;
        attrs.Add(a2);

        VkPipelineVertexInputStateCreateInfo vis = new VkPipelineVertexInputStateCreateInfo();
        vis.sType = 19u;
        vis.vertexBindingDescriptionCount = 1u;
        vis.pVertexBindingDescriptions = Native.csr_list_data(bindings);
        vis.vertexAttributeDescriptionCount = 3u;
        vis.pVertexAttributeDescriptions = Native.csr_list_data(attrs);

        VkPipelineInputAssemblyStateCreateInfo ias = new VkPipelineInputAssemblyStateCreateInfo();
        ias.sType = 20u;
        ias.topology = 3u;

        uint extW = 0u;
        uint extH = 0u;
        Native.SqwGetExtent(ref extW, ref extH);

        List<VkViewport> viewports = new List<VkViewport>();
        VkViewport vp = new VkViewport();
        vp.x = 0.0f; vp.y = 0.0f;
        vp.width = (float)extW; vp.height = (float)extH;
        vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
        viewports.Add(vp);

        List<VkRect2D> scissors = new List<VkRect2D>();
        VkRect2D sc = new VkRect2D();
        sc.offX = 0; sc.offY = 0; sc.extW = extW; sc.extH = extH;
        scissors.Add(sc);

        VkPipelineViewportStateCreateInfo vpstate = new VkPipelineViewportStateCreateInfo();
        vpstate.sType = 22u;
        vpstate.viewportCount = 1u;
        vpstate.pViewports = Native.csr_list_data(viewports);
        vpstate.scissorCount = 1u;
        vpstate.pScissors = Native.csr_list_data(scissors);

        VkPipelineRasterizationStateCreateInfo rs = new VkPipelineRasterizationStateCreateInfo();
        rs.sType = 23u;
        rs.polygonMode = 0u;
        rs.cullMode = 0u;
        rs.frontFace = 0u;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms = new VkPipelineMultisampleStateCreateInfo();
        ms.sType = 24u;
        ms.rasterizationSamples = 1u;
        ms.minSampleShading = 1.0f;

        VkPipelineDepthStencilStateCreateInfo ds = new VkPipelineDepthStencilStateCreateInfo();
        ds.sType = 25u;
        ds.depthTestEnable = 1u;
        ds.depthWriteEnable = 1u;
        ds.depthCompareOp = 1u;
        ds.minDepthBounds = 0.0f;
        ds.maxDepthBounds = 1.0f;

        List<VkPipelineColorBlendAttachmentState> blendAtt = new List<VkPipelineColorBlendAttachmentState>();
        VkPipelineColorBlendAttachmentState ba = new VkPipelineColorBlendAttachmentState();
        ba.blendEnable = 0u;
        ba.colorWriteMask = 15u;
        blendAtt.Add(ba);

        VkPipelineColorBlendStateCreateInfo cb = new VkPipelineColorBlendStateCreateInfo();
        cb.sType = 26u;
        cb.attachmentCount = 1u;
        cb.pAttachments = Native.csr_list_data(blendAtt);
        cb.blendConst0 = 1.0f; cb.blendConst1 = 1.0f; cb.blendConst2 = 1.0f; cb.blendConst3 = 1.0f;

        List<VkPushConstantRange> pcr = new List<VkPushConstantRange>();
        VkPushConstantRange rng = new VkPushConstantRange();
        rng.stageFlags = 1u;
        rng.offset = 0u;
        rng.size = 64u;
        pcr.Add(rng);

        VkPipelineLayoutCreateInfo plci = new VkPipelineLayoutCreateInfo();
        plci.sType = 30u;
        plci.pushConstantRangeCount = 1u;
        plci.pPushConstantRanges = Native.csr_list_data(pcr);
        Native.vkCreatePipelineLayout(device, ref plci, 0, out pipelineLayout);

        VkGraphicsPipelineCreateInfo gpci = new VkGraphicsPipelineCreateInfo();
        gpci.sType = 28u;
        gpci.stageCount = 2u;
        gpci.pStages = Native.csr_list_data(stages);
        gpci.pVertexInputState = vis.Ptr();
        gpci.pInputAssemblyState = ias.Ptr();
        gpci.pViewportState = vpstate.Ptr();
        gpci.pRasterizationState = rs.Ptr();
        gpci.pMultisampleState = ms.Ptr();
        gpci.pDepthStencilState = ds.Ptr();
        gpci.pColorBlendState = cb.Ptr();
        gpci.layout = pipelineLayout;
        gpci.renderPass = renderPass;
        gpci.subpass = 0u;
        gpci.basePipelineIndex = -1;
        Native.vkCreateGraphicsPipelines(device, 0, 1u, ref gpci, 0, out pipeline);

        Native.SqwRegisterRenderCallback(OnRenderPtr());
        Native.SqwRegisterClickHandler("next-btn", OnNextPtr());
        Native.SqwRegisterClickHandler("prev-btn", OnPrevPtr());
        Native.SqwSetElementText("model-title", ModelName(currentModel));
    }

    static IntPtr OnRenderPtr() { return OnRender; }
    static IntPtr OnNextPtr() { return OnNext; }
    static IntPtr OnPrevPtr() { return OnPrev; }

    public static void OnRender(IntPtr cmd, float vw, float vh) {
        angle = angle + 0.01f;
        List<float> proj = MathHelp.Perspective(0.8f, vw / vh, 0.1f, 100.0f);
        List<float> view = MathHelp.Translate(0.0f, 0.0f, -3.5f);
        List<float> rot = MathHelp.Mul(MathHelp.RotateY(angle), MathHelp.RotateX(0.3f));
        List<float> vm = MathHelp.Mul(view, rot);
        List<float> mvp = MathHelp.Mul(proj, vm);

        IntPtr vb = meshVBufs[currentModel];
        IntPtr ib = meshIBufs[currentModel];
        uint icount = meshICounts[currentModel];
        Native.vkCmdBindPipeline(cmd, 0u, pipeline);
        ulong voff = 0UL;
        Native.vkCmdBindVertexBuffers(cmd, 0u, 1u, ref vb, ref voff);
        Native.vkCmdBindIndexBuffer(cmd, ib, 0UL, 0u);
        Native.vkCmdPushConstants(cmd, pipelineLayout, 1u, 0u, 64u, Native.csr_list_data(mvp));
        Native.vkCmdDrawIndexed(cmd, icount, 1u, 0u, 0, 0u);
    }

    public static void OnNext() {
        currentModel = currentModel + 1;
        if (currentModel > 2) currentModel = 0;
        Native.SqwSetElementText("model-title", ModelName(currentModel));
    }

    public static void OnPrev() {
        currentModel = currentModel - 1;
        if (currentModel < 0) currentModel = 2;
        Native.SqwSetElementText("model-title", ModelName(currentModel));
    }
}
