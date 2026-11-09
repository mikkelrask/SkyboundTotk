#!/usr/bin/env python3
"""Compile and exercise the actual assemble_3d_slices body with recorded-copy mocks."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
source_path=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]/'a64-kit/gpu/render/rx_texture.cpp'
source=source_path.read_text()
start=source.index('void assemble_3d_slices(')
end=source.index('\nvoid assemble_from_render_targets(',start)
body=source[start:end]
pre=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <vector>
constexpr int WGPUTextureDimension_3D = 3;
struct Size {
    uint32_t width, height, depth;
};
struct TexInfo {
    Size size;
    uint32_t levels;
};
struct Image {
    int dimension = 3;
    uint64_t assembled_passes = 0;
    uint32_t layers = 32, levels = 6, width = 32, height = 32;
    int format = 1;
    bool rendered = true;
    uint64_t version = 1;
    int tex = 0;
    std::vector<uint64_t> sub_versions;
};
struct Origin {
    uint32_t x = 0, y = 0, z = 0;
};
struct WGPUTexelCopyTextureInfo {
    int texture = 0;
    uint32_t mipLevel = 0;
    Origin origin;
};
struct WGPUExtent3D {
    uint32_t width, height, depthOrArrayLayers;
};
struct Copy {
    WGPUTexelCopyTextureInfo src, dst;
    WGPUExtent3D extent;
};
std::map<uint64_t, Image> targets;
std::vector<Copy> copies;
uint64_t pass = 1;
int ends = 0;
uint64_t pass_count() { return pass; }
uint64_t nvn_slice_key(uint64_t g, uint32_t z) { return g | (uint64_t)(z + 1) << 40; }
// Distinct addresses stand in for the existing layout calculator. Tests verify
// that assembly requests each mip's address, not that calculator's internals.
uint64_t subresource_offset(const TexInfo &, uint32_t layer, uint32_t level) {
    assert(layer == 0);
    return level * 0x10000ULL;
}
Image *image_at(uint64_t k) {
    auto i = targets.find(k);
    return i == targets.end() ? nullptr : &i->second;
}
void end_render_pass(const char *) { ends++; }
int encoder() { return 0; }
void count(const char *) {}
void ce_copy_texture_to_texture(int, const WGPUTexelCopyTextureInfo *s, const WGPUTexelCopyTextureInfo *d,
                                const WGPUExtent3D *e) {
    copies.push_back({*s, *d, *e});
}
'''
post=r'''
void populate(const TexInfo &info, uint64_t guest) {
    targets.clear();
    int id = 1;
    for (uint32_t l = 0; l < info.levels; l++)
        for (uint32_t z = 0; z < std::max(1u, info.size.depth >> l); z++) {
            Image t;
            t.tex = id++;
            t.width = std::max(1u, info.size.width >> l);
            t.height = std::max(1u, info.size.height >> l);
            targets[nvn_slice_key(guest + subresource_offset(info, 0, l), z)] = t;
        }
}
int main() {
    const uint64_t guest = 0x123000;
    TexInfo info{{32, 32, 32}, 6};
    Image im;
    im.tex = 999;
    populate(info, guest);
    assemble_3d_slices(im, info, guest);
    if (copies.size() != 63) {
        std::cerr << "FAIL: expected all 63 mip/slices, got " << copies.size() << "\n";
        return 1;
    }
    size_t i = 0;
    for (uint32_t l = 0; l < 6; l++)
        for (uint32_t z = 0; z < std::max(1u, 32u >> l); z++) {
            const auto &c = copies[i++];
            assert(c.dst.texture == 999 && c.dst.mipLevel == l && c.dst.origin.z == z);
            assert(c.src.texture == targets.at(nvn_slice_key(guest + subresource_offset(info, 0, l), z)).tex);
            assert(c.extent.width == std::max(1u, 32u >> l) && c.extent.height == std::max(1u, 32u >> l) &&
                   c.extent.depthOrArrayLayers == 1);
        }
    assert(ends == 1);
    copies.clear();
    assemble_3d_slices(im, info, guest);
    assert(copies.empty());
    ++pass;
    assemble_3d_slices(im, info, guest);
    assert(copies.empty());
    auto key = nvn_slice_key(guest + subresource_offset(info, 0, 3), 2);
    ++targets[key].version;
    ++pass;
    assemble_3d_slices(im, info, guest);
    assert(copies.size() == 1 && copies[0].dst.mipLevel == 3 && copies[0].dst.origin.z == 2);
    copies.clear();
    // Reject absent, unrendered, wrong-sized, and wrong-format sources independently.
    im.sub_versions.clear();
    ++pass;
    targets.erase(nvn_slice_key(guest + subresource_offset(info, 0, 1), 0));
    targets[nvn_slice_key(guest + subresource_offset(info, 0, 1), 1)].rendered = false;
    targets[nvn_slice_key(guest + subresource_offset(info, 0, 1), 2)].width = 99;
    targets[nvn_slice_key(guest + subresource_offset(info, 0, 1), 3)].format = 2;
    assemble_3d_slices(im, info, guest);
    assert(copies.size() == 59);
    copies.clear();
    // Single-level volumes retain their original 32-slice behavior.
    populate(info, guest);
    im.levels = 1;
    im.sub_versions.clear();
    ++pass;
    assemble_3d_slices(im, info, guest);
    assert(copies.size() == 32);
    copies.clear();
    // Non-cubic volumes shrink each axis separately and retain depth 1 at later mips.
    info = {{32, 16, 4}, 6};
    populate(info, guest);
    im = Image{};
    im.width = 32;
    im.height = 16;
    im.layers = 4;
    im.tex = 999;
    ++pass;
    assemble_3d_slices(im, info, guest);
    assert(copies.size() == 10);
    assert(copies.back().extent.width == 1 && copies.back().extent.height == 1 &&
           copies.back().dst.mipLevel == 5);
    copies.clear();
    ++pass;
    assemble_3d_slices(im, info, 0);
    assert(copies.empty());
    im.dimension = 2;
    assemble_3d_slices(im, info, guest);
    assert(copies.empty());
    std::cout << "PASS: actual assembly body covers all mips, dimensions, versions, unchanged passes, "
                 "missing/mismatched sources and single-level volumes\n";
}
'''
with tempfile.TemporaryDirectory(prefix='wing-assembly-') as d:
 cpp=Path(d)/'test.cpp';exe=Path(d)/'test';cpp.write_text(pre+body+post)
 subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(cpp),'-o',str(exe)],check=True)
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
 r=subprocess.run([str(exe)],env=env);sys.exit(r.returncode)
