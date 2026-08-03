#include "vulkan_d3d8.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

uint32_t read_u32(const std::vector<uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("truncated XBE");
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

void write_u32(std::vector<uint8_t>& data, std::size_t offset, uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte)
        data[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
}

std::size_t va_offset(uint32_t address, uint32_t base,
                      const std::vector<uint8_t>& data) {
    if (address < base) throw std::runtime_error("XBE pointer precedes base");
    const std::size_t offset = static_cast<std::size_t>(address - base);
    if (offset >= data.size()) throw std::runtime_error("XBE pointer outside file");
    return offset;
}

struct xbe_summary {
    uint32_t base{};
    uint32_t entry{};
    uint32_t sections{};
};

xbe_summary inspect(const std::vector<uint8_t>& data, bool verbose) {
    if (data.size() < 0x124 || std::memcmp(data.data(), "XBEH", 4) != 0)
        throw std::runtime_error("not an Xbox XBE");

    xbe_summary result{read_u32(data, 0x104), read_u32(data, 0x118),
                       read_u32(data, 0x11c)};
    if (!result.sections || result.sections > 96)
        throw std::runtime_error("invalid XBE section count");

    const std::size_t table = va_offset(read_u32(data, 0x120), result.base, data);
    if (table + static_cast<std::size_t>(result.sections) * 0x38 > data.size())
        throw std::runtime_error("truncated XBE section table");

    if (verbose) {
        std::cout << "XBE base=0x" << std::hex << result.base
                  << " entry=0x" << result.entry << std::dec
                  << " sections=" << result.sections << '\n';
    }
    for (uint32_t index = 0; index < result.sections; ++index) {
        const std::size_t header = table + static_cast<std::size_t>(index) * 0x38;
        const uint32_t name_va = read_u32(data, header + 0x14);
        const std::size_t name_offset = va_offset(name_va, result.base, data);
        std::string name;
        for (std::size_t pos = name_offset; pos < data.size() && data[pos]; ++pos) {
            if (name.size() == 32) break;
            name.push_back(static_cast<char>(data[pos]));
        }
        if (verbose) {
            std::cout << "  " << index << " " << name
                      << " va=0x" << std::hex << read_u32(data, header + 4)
                      << " virtual=" << std::dec << read_u32(data, header + 8)
                      << " raw=" << read_u32(data, header + 0x10) << '\n';
        }
    }
    return result;
}

int self_test() {
    std::vector<uint8_t> data(0x340, 0);
    std::memcpy(data.data(), "XBEH", 4);
    write_u32(data, 0x104, 0x10000);
    write_u32(data, 0x118, 0x10184);
    write_u32(data, 0x11c, 1);
    write_u32(data, 0x120, 0x10200);
    write_u32(data, 0x204, 0x2c840);
    write_u32(data, 0x208, 4096);
    write_u32(data, 0x210, 2048);
    write_u32(data, 0x214, 0x10300);
    std::memcpy(data.data() + 0x300, ".text", 6);
    const xbe_summary result = inspect(data, false);
    if (result.base != 0x10000 || result.entry != 0x10184 || result.sections != 1)
        return 1;
    std::cout << "L.A. Rush XBE parser self-test passed\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--self-test") return self_test();

    const std::string path = argc > 1
        ? argv[1] : "game_data/L.A.Rush.USA.XBOX-ZTM/default.xbe";
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::cerr << "cannot open " << path << '\n';
        return 1;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)), {});
    try {
        inspect(data, true);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    if (argc > 2 && std::string(argv[2]) == "--gpu-smoke") {
        if (!vulkan_d3d8_init(640, 480)) {
            std::cerr << "D3D8/Vulkan initialization failed\n";
            return 1;
        }
        vulkan_d3d8_shutdown();
        std::cout << "MANXFramework Xbox D3D8 backend initialized\n";
    }
    return 0;
}
