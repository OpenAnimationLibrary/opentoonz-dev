#include "glbloader.h"

#define CGLTF_IMPLEMENTATION
#include "../../../thirdparty/cgltf/cgltf.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace otglb {
namespace {

struct Failure {
  Status status;
  std::string message;
};

[[noreturn]] void fail(Status status, const std::string &message) {
  throw Failure{status, message};
}

void require(bool condition, const char *message) {
  if (!condition) fail(Status::Invalid, message);
}

std::size_t product(std::size_t count, std::size_t size) {
  if (size && count > std::numeric_limits<std::size_t>::max() / size)
    fail(Status::ResourceLimit, "GLB allocation size overflows.");
  return count * size;
}

struct Budget {
  std::size_t limit, used = 0;
  void add(std::size_t count, std::size_t size = 1) {
    const auto bytes = product(count, size);
    if (bytes > limit - used)
      fail(Status::ResourceLimit, "GLB decoded-data memory limit exceeded.");
    used += bytes;
  }
  template <class T> void resize(std::vector<T> &out, std::size_t count) {
    add(count, sizeof(T));
    out.resize(count);
  }
  std::string string(const char *value) {
    if (!value) return {};
    add(std::strlen(value) + 1);
    std::string out(value);
    if (!out.empty()) out.resize(cgltf_decode_string(&out[0]));
    return out;
  }
};

// Headers preserve malloc's alignment, including for cgltf's double fields.
struct alignas(std::max_align_t) AllocationHeader {
  std::size_t size;
};
struct ParserBudget {
  std::size_t limit, used = 0, peak = 0;
  bool exceeded = false;
  static void *allocate(void *user, cgltf_size size) {
    auto &b = *static_cast<ParserBudget *>(user);
    if (size > std::numeric_limits<std::size_t>::max() - sizeof(AllocationHeader)) {
      b.exceeded = true;
      return nullptr;
    }
    const auto total = size + sizeof(AllocationHeader);
    if (total > b.limit - b.used) {
      b.exceeded = true;
      return nullptr;
    }
    auto *p = static_cast<AllocationHeader *>(std::malloc(total));
    if (!p) return nullptr;
    p->size = total;
    b.used += total;
    b.peak = std::max(b.peak, b.used);
    return p + 1;
  }
  static void release(void *user, void *ptr) {
    if (!ptr) return;
    auto &b = *static_cast<ParserBudget *>(user);
    auto *p = static_cast<AllocationHeader *>(ptr) - 1;
    b.used -= p->size;
    std::free(p);
  }
};

std::uint32_t little32(const unsigned char *p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

// cgltf owns parsing. Preflight only checks the envelope and bounds JSON depth
// before its recursive extension/extras processing can consume the call stack.
void preflight(const std::vector<unsigned char> &file) {
  require(file.size() >= 20, "GLB header is truncated.");
  require(little32(file.data()) == 0x46546c67, "Expected a binary GLB file.");
  if (little32(file.data() + 4) != 2)
    fail(Status::Unsupported, "Only GLB version 2 is supported.");
  require(little32(file.data() + 8) == file.size(),
          "GLB declared length does not match the file.");
  const auto jsonSize = little32(file.data() + 12);
  require(little32(file.data() + 16) == 0x4e4f534a &&
              jsonSize <= file.size() - 20 && jsonSize % 4 == 0,
          "GLB JSON chunk is invalid.");
  std::size_t chunk = 12;
  bool hasBinary = false;
  while (chunk < file.size()) {
    require(file.size() - chunk >= 8, "GLB chunk header is truncated.");
    const auto size = little32(file.data() + chunk);
    const auto type = little32(file.data() + chunk + 4);
    require(size % 4 == 0 && size <= file.size() - chunk - 8,
            "GLB chunk exceeds the file or is not aligned.");
    require(chunk == 12 || type != 0x4e4f534a,
            "GLB has more than one JSON chunk.");
    if (type == 0x004e4942) {
      require(!hasBinary && chunk == 20 + std::size_t(jsonSize),
              "GLB binary chunk is duplicated or out of order.");
      hasBinary = true;
    }
    chunk += 8 + size;
  }
  int depth = 0;
  bool quoted = false, escaped = false;
  for (std::size_t i = 20; i < 20 + std::size_t(jsonSize); ++i) {
    const auto c = file[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
    } else if (c == '"') quoted = true;
    else if (c == '[' || c == '{') {
      if (++depth > 256)
        fail(Status::ResourceLimit, "GLB JSON nesting limit exceeded.");
    } else if (c == ']' || c == '}') {
      require(--depth >= 0, "Unbalanced GLB JSON nesting.");
    }
  }
}

void warn(Result &result, const std::string &message) {
  if (std::find(result.warnings.begin(), result.warnings.end(), message) ==
      result.warnings.end())
    result.warnings.push_back(message);
}

bool supportedExtension(const char *name) {
  return name && (std::strcmp(name, "KHR_materials_unlit") == 0 ||
                  std::strcmp(name, "KHR_texture_transform") == 0 ||
                  std::strcmp(name, "KHR_mesh_quantization") == 0);
}

bool unsignedComponent(cgltf_component_type type) {
  return type == cgltf_component_type_r_8u ||
         type == cgltf_component_type_r_16u ||
         type == cgltf_component_type_r_32u;
}

// Subtraction and division avoid wrapping offsets/counts supplied by the file.
void range(const cgltf_buffer_view *view, std::size_t offset,
           std::size_t count, std::size_t stride, std::size_t element) {
  require(view && view->buffer && view->buffer->data,
          "Accessor references unavailable buffer data.");
  require(count && stride && element && offset <= view->size &&
              element <= view->size - offset &&
              count - 1 <= (view->size - offset - element) / stride,
          "Accessor range exceeds its buffer view.");
}

cgltf_accessor sparseIndices(const cgltf_accessor &a) {
  cgltf_accessor indices{};
  indices.buffer_view = a.sparse.indices_buffer_view;
  indices.offset = a.sparse.indices_byte_offset;
  indices.count = a.sparse.count;
  indices.type = cgltf_type_scalar;
  indices.component_type = a.sparse.indices_component_type;
  indices.stride = cgltf_component_size(indices.component_type);
  return indices;
}

cgltf_accessor sparseValues(const cgltf_accessor &a) {
  auto values = a;
  values.buffer_view = a.sparse.values_buffer_view;
  values.offset = a.sparse.values_byte_offset;
  values.count = a.sparse.count;
  values.stride = cgltf_calc_size(a.type, a.component_type);
  values.is_sparse = false;
  return values;
}

void validateBuffers(cgltf_data &data) {
  for (std::size_t i = 0; i < data.buffers_count; ++i) {
    auto &buffer = data.buffers[i];
    if (buffer.uri)
      fail(Status::Unsupported,
           "GLB buffer URIs are unsupported; use a self-contained GLB.");
    if (i != 0)
      fail(Status::Unsupported, "GLB must use a single embedded binary buffer.");
    require(data.bin && buffer.size <= data.bin_size,
            "GLB binary buffer is missing or truncated.");
    buffer.data = const_cast<void *>(data.bin);
    buffer.data_free_method = cgltf_data_free_method_none;
  }
  for (std::size_t i = 0; i < data.buffer_views_count; ++i) {
    const auto &view = data.buffer_views[i];
    require(view.buffer && view.buffer->data &&
                view.offset <= view.buffer->size &&
                view.size <= view.buffer->size - view.offset,
            "Buffer view exceeds its embedded buffer.");
    require(!view.stride ||
                (view.stride >= 4 && view.stride <= 252 && view.stride % 4 == 0),
            "Buffer view byte stride is invalid.");
    if (view.has_meshopt_compression) {
      const auto &c = view.meshopt_compression;
      require(c.buffer && c.offset <= c.buffer->size &&
                  c.size <= c.buffer->size - c.offset && c.stride &&
                  c.count <= std::numeric_limits<std::size_t>::max() / c.stride,
              "Meshopt extension range is invalid.");
    }
  }
  for (std::size_t i = 0; i < data.accessors_count; ++i) {
    const auto &a = data.accessors[i];
    const auto component = cgltf_component_size(a.component_type);
    const auto element = cgltf_calc_size(a.type, a.component_type);
    require(component && element && a.count &&
                a.count <= std::numeric_limits<std::size_t>::max() / 16,
            "Accessor type, component type, or count is invalid.");
    require(!a.normalized ||
                (a.component_type != cgltf_component_type_r_32f &&
                 a.component_type != cgltf_component_type_r_32u),
            "FLOAT and UNSIGNED_INT accessors cannot be normalized.");
    require(a.offset % component == 0 && a.stride >= element &&
                a.stride % component == 0,
            "Accessor is misaligned or has an invalid stride.");
    if (a.buffer_view) {
      require(a.buffer_view->offset % component == 0,
              "Accessor buffer view is misaligned.");
      range(a.buffer_view, a.offset, a.count, a.stride, element);
    } else {
      require(a.offset == 0, "Accessor without a buffer view has a byte offset.");
    }
    if (a.is_sparse) {
      const auto &s = a.sparse;
      require(s.count && s.count <= a.count &&
                  unsignedComponent(s.indices_component_type),
              "Sparse accessor count or index type is invalid.");
      const auto size = cgltf_component_size(s.indices_component_type);
      range(s.indices_buffer_view, s.indices_byte_offset, s.count, size, size);
      range(s.values_buffer_view, s.values_byte_offset, s.count, element, element);
      require(!s.indices_buffer_view->stride && !s.values_buffer_view->stride &&
                  s.indices_byte_offset % size == 0 &&
                  s.indices_buffer_view->offset % size == 0 &&
                  s.values_byte_offset % component == 0 &&
                  s.values_buffer_view->offset % component == 0,
              "Sparse accessor data is misaligned or interleaved.");
      const auto indices = sparseIndices(a);
      std::size_t previous = 0;
      for (std::size_t j = 0; j < s.count; ++j) {
        const auto index = cgltf_accessor_read_index(&indices, j);
        require(index < a.count && (!j || index > previous),
                "Sparse indices must be increasing and within the accessor.");
        previous = index;
      }
    }
  }
}

template <class T> int indexOf(const T *value, const T *base) {
  return value ? static_cast<int>(value - base) : NoIndex;
}

void finite(float value) {
  require(std::isfinite(value), "GLB contains a non-finite numeric value.");
}

template <std::size_t N> void finite(const std::array<float, N> &values) {
  for (const auto value : values) finite(value);
}

Matrix multiply(const Matrix &a, const Matrix &b) {
  Matrix out{};
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      for (int k = 0; k < 4; ++k)
        out[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
  finite(out);
  return out;
}

void include(Bounds &bounds, const std::array<float, 3> &point) {
  finite(point);
  if (bounds.empty) {
    bounds.minimum = bounds.maximum = point;
    bounds.empty = false;
  } else {
    for (int i = 0; i < 3; ++i) {
      bounds.minimum[i] = std::min(bounds.minimum[i], point[i]);
      bounds.maximum[i] = std::max(bounds.maximum[i], point[i]);
    }
  }
}

void include(Bounds &bounds, const Bounds &other) {
  if (!other.empty) {
    include(bounds, other.minimum);
    include(bounds, other.maximum);
  }
}

TextureRef textureRef(const cgltf_texture_view &v, const cgltf_data &data) {
  TextureRef out;
  out.texture = indexOf(v.texture, data.textures);
  out.texcoord = v.texcoord;
  out.strength = v.scale;
  if (v.has_transform) {
    std::copy_n(v.transform.offset, 2, out.offset.begin());
    std::copy_n(v.transform.scale, 2, out.scale.begin());
    out.rotation = v.transform.rotation;
    if (v.transform.has_texcoord) out.texcoord = v.transform.texcoord;
  }
  require(out.texcoord >= 0, "Texture coordinate set must be nonnegative.");
  finite(out.strength);
  finite(out.offset);
  finite(out.scale);
  finite(out.rotation);
  return out;
}

void copyMaterials(const cgltf_data &d, Asset &asset, Budget &budget,
                   Result &result) {
  budget.resize(asset.materials, d.materials_count);
  for (std::size_t i = 0; i < d.materials_count; ++i) {
    const auto &m = d.materials[i];
    auto &out = asset.materials[i];
    out.name = budget.string(m.name);
    std::copy_n(m.pbr_metallic_roughness.base_color_factor, 4, out.baseColor.begin());
    std::copy_n(m.emissive_factor, 3, out.emissive.begin());
    out.metallic = m.pbr_metallic_roughness.metallic_factor;
    out.roughness = m.pbr_metallic_roughness.roughness_factor;
    out.alphaCutoff = m.alpha_cutoff;
    out.alphaMode = static_cast<int>(m.alpha_mode);
    out.doubleSided = m.double_sided;
    out.unlit = m.unlit;
    out.baseColorTexture = textureRef(m.pbr_metallic_roughness.base_color_texture, d);
    out.metallicRoughnessTexture =
        textureRef(m.pbr_metallic_roughness.metallic_roughness_texture, d);
    out.normalTexture = textureRef(m.normal_texture, d);
    out.occlusionTexture = textureRef(m.occlusion_texture, d);
    out.emissiveTexture = textureRef(m.emissive_texture, d);
    finite(out.baseColor);
    finite(out.emissive);
    finite(out.metallic);
    finite(out.roughness);
    finite(out.alphaCutoff);
    require(out.alphaMode >= 0 && out.alphaMode <= 2,
            "Material alpha mode is invalid.");
  }
  budget.resize(asset.images, d.images_count);
  for (std::size_t i = 0; i < d.images_count; ++i) {
    const auto &im = d.images[i];
    if (im.uri)
      fail(Status::Unsupported,
           "Image URIs (including data URIs) are unsupported; embed images in GLB buffer views.");
    require(im.buffer_view && im.mime_type,
            "Embedded image needs a buffer view and MIME type.");
    auto &out = asset.images[i];
    out.name = budget.string(im.name);
    out.mimeType = budget.string(im.mime_type);
    budget.resize(out.encoded, im.buffer_view->size);
    if (!out.encoded.empty())
      std::memcpy(out.encoded.data(), cgltf_buffer_view_data(im.buffer_view),
                  out.encoded.size());
  }
  budget.resize(asset.textures, d.textures_count);
  for (std::size_t i = 0; i < d.textures_count; ++i) {
    const auto &t = d.textures[i];
    auto &out = asset.textures[i];
    if (!t.image && (t.has_basisu || t.has_webp))
      fail(Status::Unsupported,
           "Compressed/extended texture has no supported core image fallback.");
    out.image = indexOf(t.image, d.images);
    if (t.sampler) {
      out.magFilter = t.sampler->mag_filter;
      out.minFilter = t.sampler->min_filter;
      out.wrapS = t.sampler->wrap_s;
      out.wrapT = t.sampler->wrap_t;
    }
    if (t.has_basisu || t.has_webp)
      warn(result, "Optional texture extension ignored; core image fallback retained.");
  }
}

Attribute copyAttribute(const cgltf_attribute &attribute, Budget &budget) {
  const auto &a = *attribute.data;
  Attribute out;
  out.semantic = budget.string(attribute.name);
  out.components = static_cast<int>(cgltf_num_components(a.type));
  const auto count = product(a.count, std::size_t(out.components));
  budget.resize(out.values, count);
  auto base = a;
  base.is_sparse = false;
  require(cgltf_accessor_unpack_floats(&base, out.values.data(), count) == count,
          "Could not decode mesh attribute.");
  // Sparse values are tightly packed even when the base accessor is interleaved.
  if (a.is_sparse) {
    const auto indices = sparseIndices(a);
    const auto values = sparseValues(a);
    for (std::size_t i = 0; i < a.sparse.count; ++i) {
      const auto index = cgltf_accessor_read_index(&indices, i);
      require(cgltf_accessor_read_float(&values, i,
                  out.values.data() + index * out.components, out.components),
              "Could not decode sparse mesh attribute.");
    }
  }
  for (const auto value : out.values) finite(value);
  return out;
}

void copyMeshes(const cgltf_data &d, Asset &asset, Budget &budget, Result &result) {
  budget.resize(asset.meshes, d.meshes_count);
  for (std::size_t i = 0; i < d.meshes_count; ++i) {
    auto &mesh = asset.meshes[i];
    const auto &source = d.meshes[i];
    mesh.name = budget.string(source.name);
    budget.resize(mesh.primitives, source.primitives_count);
    for (std::size_t j = 0; j < source.primitives_count; ++j) {
      const auto &p = source.primitives[j];
      auto &out = mesh.primitives[j];
      out.mode = static_cast<int>(p.type) - 1;
      out.material = indexOf(p.material, d.materials);
      out.hasMorphTargets = p.targets_count != 0;
      const auto *positions = cgltf_find_accessor(&p, cgltf_attribute_type_position, 0);
      require(positions && positions->type == cgltf_type_vec3,
              "Mesh primitive requires a VEC3 POSITION attribute.");
      out.vertexCount = positions->count;
      if (p.has_draco_mesh_compression) {
        for (std::size_t k = 0; k < p.attributes_count; ++k)
          if (!p.attributes[k].data->buffer_view && !p.attributes[k].data->is_sparse)
            fail(Status::Unsupported, "Draco geometry has no uncompressed fallback.");
        if (p.indices && !p.indices->buffer_view && !p.indices->is_sparse)
          fail(Status::Unsupported, "Draco indices have no uncompressed fallback.");
        warn(result, "Optional Draco compression ignored; uncompressed geometry retained.");
      }
      budget.resize(out.attributes, p.attributes_count);
      for (std::size_t k = 0; k < p.attributes_count; ++k) {
        require(p.attributes[k].data->count == out.vertexCount,
                "Mesh attribute counts disagree.");
        for (std::size_t n = 0; n < k; ++n)
          require(std::strcmp(p.attributes[k].name, p.attributes[n].name) != 0,
                  "Mesh contains duplicate attribute semantics.");
        out.attributes[k] = copyAttribute(p.attributes[k], budget);
        if (p.attributes[k].data == positions) {
          const auto &v = out.attributes[k].values;
          for (std::size_t n = 0; n < v.size(); n += 3)
            include(out.bounds, std::array<float, 3>{{v[n], v[n + 1], v[n + 2]}});
        }
      }
      if (p.indices) {
        const auto &a = *p.indices;
        require(a.type == cgltf_type_scalar && unsignedComponent(a.component_type) &&
                    !a.normalized,
                "Mesh indices must be unsigned, unnormalized scalar integers.");
        budget.resize(out.indices, a.count);
        auto base = a;
        base.is_sparse = false;
        for (std::size_t k = 0; k < a.count; ++k)
          out.indices[k] = static_cast<std::uint32_t>(cgltf_accessor_read_index(&base, k));
        if (a.is_sparse) {
          const auto indices = sparseIndices(a);
          const auto values = sparseValues(a);
          for (std::size_t k = 0; k < a.sparse.count; ++k)
            out.indices[cgltf_accessor_read_index(&indices, k)] =
                static_cast<std::uint32_t>(cgltf_accessor_read_index(&values, k));
        }
        for (const auto index : out.indices)
          require(index < out.vertexCount, "Mesh index exceeds its vertex count.");
      }
      const auto count = p.indices ? out.indices.size() : out.vertexCount;
      if (out.mode == 4) {
        require(count % 3 == 0, "Triangle primitive has an incomplete triangle.");
        out.triangleCount = count / 3;
      } else if (out.mode == 5 || out.mode == 6) {
        require(count >= 3, "Triangle strip/fan needs at least three vertices.");
        out.triangleCount = count - 2;
      } else {
        require(out.mode >= 0 && out.mode <= 3,
                "Primitive topology is invalid.");
        require(out.mode != 1 || count % 2 == 0,
                "Line primitive has an incomplete segment.");
        warn(result, "Point/line topology retained; no triangle surface is generated.");
      }
      if (out.hasMorphTargets)
        warn(result, "Morph targets are not evaluated; base geometry retained.");
      include(mesh.bounds, out.bounds);
    }
  }
}

void copyNodes(const cgltf_data &d, Asset &asset, Budget &budget,
               const Limits &limits, Result &result) {
  budget.resize(asset.nodes, d.nodes_count);
  std::vector<int> order;
  budget.add(d.nodes_count, sizeof(int));
  order.reserve(d.nodes_count);
  std::vector<std::size_t> depths;
  budget.resize(depths, d.nodes_count);
  for (std::size_t i = 0; i < d.nodes_count; ++i) {
    const auto &n = d.nodes[i];
    auto &out = asset.nodes[i];
    out.name = budget.string(n.name);
    out.mesh = indexOf(n.mesh, d.meshes);
    out.parent = indexOf(n.parent, d.nodes);
    out.hasSkin = n.skin != nullptr;
    require(!n.has_matrix || !(n.has_translation || n.has_rotation || n.has_scale),
            "Node cannot specify both matrix and TRS transforms.");
    cgltf_node_transform_local(&n, out.local.data());
    finite(out.local);
    require(out.local[3] == 0 && out.local[7] == 0 &&
                out.local[11] == 0 && out.local[15] == 1,
            "Node transform must be affine.");
    budget.resize(out.children, n.children_count);
    for (std::size_t j = 0; j < n.children_count; ++j)
      out.children[j] = indexOf(n.children[j], d.nodes);
    if (out.parent == NoIndex) {
      order.push_back(static_cast<int>(i));
      depths[i] = 1;
    }
    if (n.has_mesh_gpu_instancing)
      warn(result, "GPU instancing is not evaluated; base node retained.");
  }
  // Iterative topological traversal detects cycles before cgltf_validate and
  // computes each world matrix once, regardless of model hierarchy depth.
  for (std::size_t i = 0; i < order.size(); ++i) {
    const int index = order[i];
    auto &n = asset.nodes[index];
    if (depths[index] > limits.hierarchyDepth)
      fail(Status::ResourceLimit, "GLB hierarchy depth limit exceeded.");
    n.world = n.parent == NoIndex ? n.local :
        multiply(asset.nodes[n.parent].world, n.local);
    for (const auto child : n.children) {
      require(!depths[child], "Node hierarchy repeats a node or contains a cycle.");
      depths[child] = depths[index] + 1;
      order.push_back(child);
    }
  }
  require(order.size() == asset.nodes.size(), "Node hierarchy contains a cycle.");
}

void copyScenes(const cgltf_data &d, Asset &asset, Budget &budget, Result &result) {
  const bool synthetic = d.scenes_count == 0 && d.nodes_count != 0;
  budget.resize(asset.scenes, synthetic ? 1 : d.scenes_count);
  asset.defaultScene = indexOf(d.scene, d.scenes);
  if (synthetic) {
    auto &scene = asset.scenes[0];
    scene.name = budget.string("Root nodes");
    std::size_t roots = 0;
    for (const auto &n : asset.nodes) roots += n.parent == NoIndex;
    budget.resize(scene.roots, roots);
    std::size_t next = 0;
    for (std::size_t i = 0; i < asset.nodes.size(); ++i)
      if (asset.nodes[i].parent == NoIndex) scene.roots[next++] = static_cast<int>(i);
    asset.defaultScene = 0;
    warn(result, "No scenes declared; parentless nodes form an inspection scene.");
  } else {
    for (std::size_t i = 0; i < d.scenes_count; ++i) {
      auto &scene = asset.scenes[i];
      scene.name = budget.string(d.scenes[i].name);
      budget.resize(scene.roots, d.scenes[i].nodes_count);
      for (std::size_t j = 0; j < d.scenes[i].nodes_count; ++j)
        scene.roots[j] = indexOf(d.scenes[i].nodes[j], d.nodes);
    }
  }
  std::vector<int> order;
  budget.add(asset.nodes.size(), sizeof(int));
  order.reserve(asset.nodes.size());
  std::vector<Bounds> subtree;
  budget.resize(subtree, asset.nodes.size());
  for (std::size_t i = 0; i < asset.nodes.size(); ++i)
    if (asset.nodes[i].parent == NoIndex) order.push_back(static_cast<int>(i));
  for (std::size_t i = 0; i < order.size(); ++i) {
    const auto index = order[i];
    const auto &n = asset.nodes[index];
    for (const auto child : n.children) order.push_back(child);
    if (n.mesh == NoIndex) continue;
    for (const auto &p : asset.meshes[n.mesh].primitives)
      for (const auto &a : p.attributes) {
        if (a.semantic != "POSITION") continue;
        const auto &m = n.world;
        for (std::size_t j = 0; j < a.values.size(); j += 3) {
          const auto x = a.values[j], y = a.values[j + 1], z = a.values[j + 2];
          include(subtree[index], std::array<float, 3>{{
              m[0] * x + m[4] * y + m[8] * z + m[12],
              m[1] * x + m[5] * y + m[9] * z + m[13],
              m[2] * x + m[6] * y + m[10] * z + m[14]}});
        }
      }
  }
  // Reuse exact world bounds for roots shared by multiple scenes. Taking a
  // transformed local AABB would overestimate rotated non-box geometry.
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    const auto parent = asset.nodes[*it].parent;
    if (parent != NoIndex) include(subtree[parent], subtree[*it]);
  }
  std::vector<std::size_t> visited;
  budget.resize(visited, asset.nodes.size());
  for (std::size_t i = 0; i < asset.scenes.size(); ++i) {
    auto &scene = asset.scenes[i];
    for (const auto root : scene.roots) {
      require(asset.nodes[root].parent == NoIndex && visited[root] != i + 1,
              "Scene root is duplicated or has a parent.");
      visited[root] = i + 1;
      include(scene.bounds, subtree[root]);
    }
  }
}

}  // namespace

Result load(const std::filesystem::path &path, const Limits &limits) {
  const auto start = std::chrono::steady_clock::now();
  Result result;
  ParserBudget parser{limits.parserBytes};
  Budget decoded{limits.decodedBytes};
  try {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) fail(Status::IoError, "Cannot open GLB file for reading.");
    const auto end = stream.tellg();
    if (end < 0) fail(Status::IoError, "Cannot determine GLB file size.");
    const auto bytes = static_cast<std::uintmax_t>(end);
    if (bytes > limits.fileBytes || bytes > std::numeric_limits<std::uint32_t>::max() ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
      fail(Status::ResourceLimit, "GLB file-size limit exceeded.");
    result.fileBytes = static_cast<std::size_t>(bytes);
    std::vector<unsigned char> file(result.fileBytes);
    stream.seekg(0);
    if (!file.empty() && !stream.read(reinterpret_cast<char *>(file.data()), file.size()))
      fail(Status::IoError, "Cannot read the complete GLB file.");
    preflight(file);
    cgltf_options options{};
    options.type = cgltf_file_type_glb;
    options.memory = {ParserBudget::allocate, ParserBudget::release, &parser};
    cgltf_data *raw = nullptr;
    const auto parsed = cgltf_parse(&options, file.data(), file.size(), &raw);
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    if (parsed != cgltf_result_success) {
      if (parser.exceeded || parsed == cgltf_result_out_of_memory)
        fail(Status::ResourceLimit, "GLB parser memory limit exceeded or allocation failed.");
      fail(Status::Invalid, "cgltf could not parse the GLB structure.");
    }
    require(data->asset.version && std::strcmp(data->asset.version, "2.0") == 0,
            "GLB asset must declare glTF version 2.0.");
    if (data->asset.min_version && std::strcmp(data->asset.min_version, "2.0") != 0)
      fail(Status::Unsupported, "GLB requires a newer glTF asset version.");
    for (std::size_t i = 0; i < data->extensions_required_count; ++i)
      if (!supportedExtension(data->extensions_required[i]))
        fail(Status::Unsupported, "Unsupported required extension: " +
             std::string(data->extensions_required[i]));
    for (std::size_t i = 0; i < data->extensions_used_count; ++i)
      if (!supportedExtension(data->extensions_used[i]))
        warn(result, "Optional extension ignored; core fallback retained: " +
             std::string(data->extensions_used[i]));
    for (const auto count : {data->nodes_count, data->meshes_count,
              data->materials_count, data->scenes_count,
              data->images_count, data->textures_count})
      if (count > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        fail(Status::ResourceLimit, "GLB object index limit exceeded.");
    validateBuffers(*data);
    decoded.add(1, sizeof(Asset));
    auto asset = std::make_shared<Asset>();
    copyNodes(*data, *asset, decoded, limits, result);
    require(cgltf_validate(data.get()) == cgltf_result_success,
            "cgltf validation failed: invalid GLB references, geometry, or hierarchy.");
    copyMaterials(*data, *asset, decoded, result);
    copyMeshes(*data, *asset, decoded, result);
    copyScenes(*data, *asset, decoded, result);
    if (std::none_of(asset->meshes.begin(), asset->meshes.end(),
                    [](const Mesh &mesh) { return !mesh.primitives.empty(); }))
      warn(result, "No mesh geometry is present in this GLB.");
    asset->animationCount = data->animations_count;
    asset->skinCount = data->skins_count;
    if (asset->animationCount)
      warn(result, "Animations are not evaluated; default node transforms retained.");
    if (asset->skinCount)
      warn(result, "Skinning is not evaluated; undeformed mesh geometry retained.");
    if (data->cameras_count || data->lights_count)
      warn(result, "Embedded cameras/lights are not retained in this loader stage.");
    result.asset = std::move(asset);
    result.status = result.warnings.empty() ? Status::Loaded : Status::LoadedWithWarnings;
  } catch (const Failure &failure) {
    result.status = failure.status;
    result.error = failure.message;
  } catch (const std::bad_alloc &) {
    result.status = Status::ResourceLimit;
    result.error = "Not enough memory to load GLB.";
  } catch (const std::length_error &) {
    result.status = Status::ResourceLimit;
    result.error = "GLB container exceeds allocation limits.";
  } catch (const std::filesystem::filesystem_error &) {
    result.status = Status::IoError;
    result.error = "Cannot access GLB filesystem path.";
  }
  result.parserPeakBytes = parser.peak;
  result.decodedBytes = result.asset ? decoded.used : 0;
  result.loadMilliseconds = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
  return result;
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Loaded: return "loaded";
  case Status::LoadedWithWarnings: return "loaded-with-warnings";
  case Status::Invalid: return "invalid";
  case Status::Unsupported: return "unsupported";
  case Status::IoError: return "io-error";
  case Status::ResourceLimit: return "resource-limit";
  }
  return "unknown";
}

}  // namespace otglb
