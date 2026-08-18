int main(void) {
    static char invalid_chunk[] = "XXXX PNG chunk not known";
    invalid_chunk[0] = 'A';
    return (int)invalid_chunk[0];
}
