// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
#include <sstream>
#include <numeric>
#include <unordered_set>

#include "obj-export.hh"
#include "common-macros.inc"
#include "tiny-format.hh"

namespace tinyusdz {
namespace tydra {

#define PushError(msg) { \
  if (err) { \
    (*err) += msg + "\n"; \
  } \
}

bool export_to_obj(const RenderScene &scene, const int mesh_id,
  std::string &obj_str, std::string &mtl_str, std::string *warn, std::string *err) {

  //
  // NOTE:
  //
  // - Export GeomSubset(per-face material) as group(g) + usemtl
  //

  (void)obj_str;
  (void)mtl_str;
  (void)warn;

  std::stringstream ss;

  if (mesh_id < 0) {
    PUSH_ERROR_AND_RETURN("Invalid mesh_id");
  } else if (size_t(mesh_id) >= scene.meshes.size()) {
    PUSH_ERROR_AND_RETURN(fmt::format("mesh_id {} is out-of-range. scene.meshes.size {}", mesh_id, scene.meshes.size()));
  }

  const RenderMesh &mesh = scene.meshes[size_t(mesh_id)];

  ss << "# exported from TinyUSDZ Tydra.\n";
  ss << "mtllib " << mesh.prim_name + ".mtl";
  ss << "\n";
  
  for (size_t i = 0; i < mesh.points.size(); i++) {
    ss << "v " << mesh.points[i][0] << " " << mesh.points[i][1] << " " << mesh.points[i][2] << "\n";
  } 
  ss << "# " << mesh.points.size() << " vertices\n";

  bool is_single_indexed = true;
  bool has_texcoord = false;
  bool has_normal = false;

  // primary texcoord only
  if (mesh.texcoords.count(0)) {
    const VertexAttribute &texcoord = mesh.texcoords.at(0);  
    if (texcoord.format == VertexAttributeFormat::Vec2) {
      const float *ptr = reinterpret_cast<const float *>(texcoord.buffer());
      for (size_t i = 0; i < texcoord.vertex_count(); i++) {
        ss << "vt " << ptr[2 * i + 0] << " " << ptr[2 * i + 1] << "\n";
      } 

      is_single_indexed &= texcoord.is_vertex();
      has_texcoord = true;
    }
  }

  if (!mesh.normals.empty()) {
    if (mesh.normals.format == VertexAttributeFormat::Vec3) {
      const float *ptr = reinterpret_cast<const float *>(mesh.normals.buffer());
      for (size_t i = 0; i < mesh.normals.vertex_count(); i++) {
        ss << "vn " << ptr[3 * i + 0] << " " << ptr[3 * i + 1] << " " << ptr[3 * i + 2] << "\n";
      } 
      is_single_indexed &= mesh.normals.is_vertex();
      has_normal = true;
    }
  }

  if (!is_single_indexed) {
    PUSH_ERROR_AND_RETURN("TODO: mesh with facevarying texcoord or facevarying normal");
  }

  // name -> (mat_id, face_ids)
  std::unordered_map<std::string, std::pair<int, std::vector<uint32_t>>> face_groups;
  if (mesh.material_subsetMap.size()) {

    std::unordered_set<uint32_t> subset_face_ids;

    for (const auto &subset : mesh.material_subsetMap) {
      std::vector<uint32_t> face_ids(subset.second.indices().size());
      for (size_t i = 0; i < subset.second.indices().size(); i++) {
        face_ids[i] = uint32_t(subset.second.indices()[i]); 
        subset_face_ids.insert(face_ids[i]);
      }
      if (subset.first.empty()) {
        PUSH_ERROR_AND_RETURN("Empty material_subset name is not allowed.");
      }
      face_groups[subset.first] = std::make_pair(subset.second.material_id, face_ids);
    }

    // face_ids without materialsubset
    std::vector<uint32_t> face_ids;
    for (size_t i = 0; i < mesh.faceVertexCounts().size(); i++) {
      if (!subset_face_ids.count(uint32_t(i))) {
        face_ids.push_back(uint32_t(i));
      }
    }
    face_groups[""] = std::make_pair(mesh.material_id, face_ids);
  } else {
    std::vector<uint32_t> face_ids(mesh.faceVertexCounts().size());
    std::iota(face_ids.begin(), face_ids.end(), 0);

    face_groups[""] = std::make_pair(mesh.material_id, face_ids);
  }

  // build face_id -> location in mesh.faceVertexIndices table.
  std::vector<size_t> offsets(mesh.faceVertexCounts().size());
  size_t offset = 0;
  for (size_t i = 0; i < mesh.faceVertexCounts().size(); i++) {
    offsets[i] = offset;
    offset += mesh.faceVertexCounts()[i];
  }

  // Assume empty group name is iterated first.
  for (const auto &group : face_groups) {

    if (group.first.size()) {
      ss << "g " << group.first << "\n";
    }

    if (std::get<0>(group.second) > -1) {
      uint32_t mat_id = uint32_t(std::get<0>(group.second));
      // prepend mat_id to name
      ss << "usemtl " << mat_id << scene.materials[mat_id].name << "\n";
    } 

    const auto &face_ids = std::get<1>(group.second);

    for (size_t i = 0; i < face_ids.size(); i++) {
      ss << "f ";

      for (size_t f = 0; f < mesh.faceVertexCounts()[face_ids[i]]; f++) {
        if (f > 0) {
          ss << " ";
        }
        // obj's index starts with 1.
        uint32_t idx = mesh.faceVertexIndices()[offsets[face_ids[i]] + f] + 1;

        if (has_texcoord && has_normal) {
          ss << idx << "/" << idx << "/" << idx;
        } else if (has_texcoord) {
          ss << idx << "/" << idx;
        } else if (has_normal) {
          ss << idx << "//" << idx;
        } else {
          ss << idx;
        }
      }

      ss << "\n";
    }

    ss << "\n";
  }

  obj_str = ss.str();

  ss.str("");
  ss << "# exported from TinyUSDZ Tydra.\n";

  // emit material info
  for (const auto &group : face_groups) {
    if (group.second.first == -1) {
      continue;
    }

    uint32_t mat_id = uint32_t(std::get<0>(group.second));
    ss << "newmtl " << scene.materials[mat_id].name << "\n";

    // Diffuse only
    // TODO: Emit more PBR material
    if (scene.materials[mat_id].surfaceShader.diffuseColor.is_texture()) {
      int32_t texId = scene.materials[mat_id].surfaceShader.diffuseColor.textureId;
      if ((texId < 0) || (texId >= int(scene.textures.size()))) {
        PUSH_ERROR_AND_RETURN(fmt::format("Invalid texture id {}. scene.textures.size = {}", texId, scene.textures.size()));
      }

      int64_t imageId = scene.textures[size_t(texId)].texture_image_id;
      if ((imageId < 0) || (imageId >= int64_t(scene.images.size()))) {
        PUSH_ERROR_AND_RETURN(fmt::format("Invalid image id {}. scene.images.size = {}", imageId, scene.images.size()));
      }

      std::string texname = scene.images[size_t(imageId)].asset_identifier;
      if (texname.empty()) {
        PUSH_ERROR_AND_RETURN(fmt::format("Filename for image id {} is empty.", imageId));
      }
      ss << "map_Kd " << texname << "\n";
    } else {
      const auto col = scene.materials[mat_id].surfaceShader.diffuseColor.value;
      ss << "Kd " << col[0] << " " << col[1] << " " << col[2] << "\n";
    }

    ss << "\n";
  }
  ss << "# " << face_groups.size() << " materials.\n";

  mtl_str = ss.str();

  return true;
}


} // namespace tydra
} // namespace tinyusdz
