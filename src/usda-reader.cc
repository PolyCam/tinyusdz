// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stack>
#if defined(__wasi__)
#else
#include <thread>
#endif
#include <vector>

#include "usda-reader.hh"

//
#if !defined(TINYUSDZ_DISABLE_MODULE_USDA_READER)

//

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// external
//#include "ryu/ryu.h"
//#include "ryu/ryu_parse.h"

#include "fast_float/fast_float.h"
#include "nonstd/expected.hpp"
#include "nonstd/optional.hpp"

//

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//

// Tentative
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif

#include "io-util.hh"
#include "math-util.inc"
#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
//#include "simple-type-reflection.hh"
#include "primvar.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "usdObj.hh"
#include "value-pprint.hh"
#include "value-type.hh"

// s = std::string
#define PUSH_ERROR_AND_RETURN(s)                                   \
  do {                                                             \
    std::ostringstream ss;                                         \
    ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                       \
    _err += ss.str();                                           \
    return false;                                                  \
  } while (0)

#define PUSH_WARN(s)                                   \
  do {                                                             \
    std::ostringstream ss;                                         \
    ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                       \
    _err += ss.str();                                           \
    return false;                                                  \
  } while (0)

#if defined(TINYUSDZ_PRODUCTION_BUILD)
#define TINYUSDZ_LOCAL_DEBUG_PRINT
#endif

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
#define DCOUT(x)                                               \
  do {                                                         \
    std::cout << __FILE__ << ":" << __func__ << ":"            \
              << std::to_string(__LINE__) << " " << x << "\n"; \
  } while (false)
#else
#define DCOUT(x)
#endif

namespace tinyusdz {

namespace usda {


class VariableDef {
 public:
  std::string type;
  std::string name;

  VariableDef() = default;

  VariableDef(const std::string &t, const std::string &n) : type(t), name(n) {}

  VariableDef(const VariableDef &rhs) = default;

  VariableDef &operator=(const VariableDef &rhs) {
    type = rhs.type;
    name = rhs.name;

    return *this;
  }
};


namespace {

using ReferenceList = std::vector<std::pair<ListEditQual, Reference>>;

#if 0
// Extract array of References from Variable.
ReferenceList GetReferences(
    const std::tuple<ListEditQual, value::any_value> &_var) {
  ReferenceList result;

  ListEditQual qual = std::get<0>(_var);

  auto var = std::get<1>(_var);

  SDCOUT << "GetReferences. var.name = " << var.name << "\n";

  if (var.IsArray()) {
    DCOUT("IsArray");
    auto parr = var.as_array();
    if (parr) {
      DCOUT("parr");
      for (const auto &v : parr->values) {
        DCOUT("Maybe Value");
        if (v.IsValue()) {
          DCOUT("Maybe Reference");
          if (auto pref = nonstd::get_if<Reference>(v.as_value())) {
            DCOUT("Got it");
            result.push_back({qual, *pref});
          }
        }
      }
    }
  } else if (var.IsValue()) {
    DCOUT("IsValue");
    if (auto pv = var.as_value()) {
      DCOUT("Maybe Reference");
      if (auto pas = nonstd::get_if<Reference>(pv)) {
        DCOUT("Got it");
        result.push_back({qual, *pas});
      }
    }
  } else {
    DCOUT("Unknown var type: " + Variable::type_name(var));
  }

  return result;
}
#endif


}  // namespace


inline bool hasConnect(const std::string &str) {
  return endsWith(str, ".connect");
}

inline bool hasInputs(const std::string &str) {
  return startsWith(str, "inputs:");
}

inline bool hasOutputs(const std::string &str) {
  return startsWith(str, "outputs:");
}

class USDAReader::Impl {
 private:
  HighLevelScene scene_;

 public:

  Impl(StreamReader *sr) {
    _parser.SetStream(sr);
  }

  // Return the flag if the .usda is read from `references`
  bool IsReferenced() { return _referenced; }

  // Return the flag if the .usda is read from `subLayers`
  bool IsSubLayered() { return _sub_layered; }

  // Return the flag if the .usda is read from `payload`
  bool IsPayloaded() { return _payloaded; }

  // Return true if the .udsa is read in the top layer(stage)
  bool IsToplevel() {
    return !IsReferenced() && !IsSubLayered() && !IsPayloaded();
  }

  void SetBaseDir(const std::string &str) { _base_dir = str; }

  std::string GetCurrentPath() {
    if (_path_stack.empty()) {
      return "/";
    }

    return _path_stack.top();
  }

  bool PathStackDepth() { return _path_stack.size(); }

  void PushPath(const std::string &p) { _path_stack.push(p); }

  void PopPath() {
    if (!_path_stack.empty()) {
      _path_stack.pop();
    }
  }


#if 0
    if (prim_type.empty()) {
      if (IsToplevel()) {
        if (references.size()) {
          // Infer prim type from referenced asset.

          if (references.size() > 1) {
            LOG_ERROR("TODO: multiple references\n");
          }

          auto it = references.begin();
          const Reference &ref = it->second;
          std::string filepath = ref.asset_path;

          // usdOBJ?
          if (endsWith(filepath, ".obj")) {
            prim_type = "geom_mesh";
          } else {
            if (!io::IsAbsPath(filepath)) {
              filepath = io::JoinPath(_base_dir, ref.asset_path);
            }

            if (_reference_cache.count(filepath)) {
              LOG_ERROR("TODO: Use cached info");
            }

            DCOUT("Reading references: " + filepath);

            std::vector<uint8_t> data;
            std::string err;
            if (!io::ReadWholeFile(&data, &err, filepath,
                                   /* max_filesize */ 0)) {
              PUSH_ERROR_AND_RETURN("Failed to read file: " + filepath);
            }

            tinyusdz::StreamReader sr(data.data(), data.size(),
                                      /* swap endian */ false);
            tinyusdz::usda::USDAReader parser(&sr);

            std::string base_dir = io::GetBaseDir(filepath);

            parser.SetBaseDir(base_dir);

            {
              bool ret = parser.Parse(tinyusdz::usda::LOAD_STATE_REFERENCE);

              if (!ret) {
                PUSH_WARN("Failed to parse .usda: " << parser.GetError());
              } else {
                DCOUT("`references` load ok.");
              }
            }

            std::string defaultPrim = parser.GetDefaultPrimName();

            DCOUT("defaultPrim: " + parser.GetDefaultPrimName());

            const std::vector<GPrim> &root_nodes = parser.GetGPrims();
            if (root_nodes.empty()) {
              PUSH_WARN("USD file does not contain any Prim node.");
            } else {
              size_t default_idx =
                  0;  // Use the first element when corresponding defaultPrim
                      // node is not found.

              auto node_it = std::find_if(root_nodes.begin(), root_nodes.end(),
                                          [defaultPrim](const GPrim &a) {
                                            return !defaultPrim.empty() &&
                                                   (a.name == defaultPrim);
                                          });

              if (node_it != root_nodes.end()) {
                default_idx =
                    size_t(std::distance(root_nodes.begin(), node_it));
              }

              DCOUT("defaultPrim node: " + root_nodes[default_idx].name);
              for (size_t i = 0; i < root_nodes.size(); i++) {
                DCOUT("root nodes: " + root_nodes[i].name);
              }

              // Store result to cache
              _reference_cache[filepath] = {default_idx, root_nodes};

              prim_type = root_nodes[default_idx].prim_type;
              DCOUT("Infered prim type: " + prim_type);
            }
          }
        }
      } else {
        // Unknown or unresolved node type
        LOG_ERROR("TODO: unresolved node type\n");
      }
    }

    if (IsToplevel()) {
      if (prim_type.empty()) {
        // Reconstuct Generic Prim.

        GPrim gprim;
        if (!ReconstructGPrim(props, references, &gprim)) {
          PushError("Failed to reconstruct GPrim.");
          return false;
        }
        gprim.name = node_name;
        scene_.root_nodes.emplace_back(gprim);


      } else {

        // Reconstruct concrete C++ object
#if 0

#define RECONSTRUCT_NODE(__tyname, __reconstruct_fn, __dty, __scene) \
        } else if (prim_type == __tyname) { \
          __dty node; \
          if (!__reconstruct_fn(props, references, &node)) { \
            PUSH_ERROR_AND_RETURN("Failed to reconstruct " << __tyname); \
          } \
          node.name = node_name; \
          __scene.emplace_back(node);

        if (0) {
        RECONSTRUCT_NODE("Xform", ReconstructXform, Xform, scene_.xforms)
        RECONSTRUCT_NODE("Mesh", ReconstructGeomMesh, GeomMesh, scene_.geom_meshes)
        RECONSTRUCT_NODE("Sphere", ReconstructGeomSphere, GeomSphere, scene_.geom_spheres)
        RECONSTRUCT_NODE("Cone", ReconstructGeomCone, GeomCone, scene_.geom_cones)
        RECONSTRUCT_NODE("Cube", ReconstructGeomCube, GeomCube, scene_.geom_cubes)
        RECONSTRUCT_NODE("Capsule", ReconstructGeomCapsule, GeomCapsule, scene_.geom_capsules)
        RECONSTRUCT_NODE("Cylinder", ReconstructGeomCylinder, GeomCylinder, scene_.geom_cylinders)
        RECONSTRUCT_NODE("BasisCurves", ReconstructBasisCurves, GeomBasisCurves, scene_.geom_basis_curves)
        RECONSTRUCT_NODE("Camera", ReconstructGeomCamera, GeomCamera, scene_.geom_cameras)
        RECONSTRUCT_NODE("Shader", ReconstructShader, Shader, scene_.shaders)
        RECONSTRUCT_NODE("NodeGraph", ReconstructNodeGraph, NodeGraph, scene_.node_graphs)
        RECONSTRUCT_NODE("Material", ReconstructMaterial, Material, scene_.materials)

        RECONSTRUCT_NODE("Scope", ReconstructScope, Scope, scene_.scopes)

        RECONSTRUCT_NODE("SphereLight", ReconstructLuxSphereLight, LuxSphereLight, scene_.lux_sphere_lights)
        RECONSTRUCT_NODE("DomeLight", ReconstructLuxDomeLight, LuxDomeLight, scene_.lux_dome_lights)

        RECONSTRUCT_NODE("SkelRoot", ReconstructSkelRoot, SkelRoot, scene_.skel_roots)
        RECONSTRUCT_NODE("Skeleton", ReconstructSkeleton, Skeleton, scene_.skeletons)
        } else {
          PUSH_ERROR_AND_RETURN(" TODO: " + prim_type);
        }
#endif
        PUSH_ERROR_AND_RETURN(" TODO: " + prim_type);
      }
    } else {
      // Store properties to GPrim.
      // TODO: Use Class?
      GPrim gprim;
      if (!ReconstructGPrim(props, references, &gprim)) {
        PushError("Failed to reconstruct GPrim.");
        return false;
      }
      gprim.name = node_name;
      gprim.prim_type = prim_type;

      if (PathStackDepth() == 1) {
        // root node
        _gprims.push_back(gprim);
      }

    }

    PopPath();

    return true;
  }
#endif

  bool ReconstructGPrim(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GPrim *gprim);

  bool ReconstructGeomSphere(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomSphere *sphere);

  bool ReconstructGeomCone(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomCone *cone);

  bool ReconstructGeomCube(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomCube *cube);

  bool ReconstructGeomCapsule(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomCapsule *capsule);

  bool ReconstructGeomCylinder(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomCylinder *cylinder);

  bool ReconstructXform(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      Xform *xform);

  bool ReconstructGeomMesh(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomMesh *mesh);

  bool ReconstructBasisCurves(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomBasisCurves *curves);

  bool ReconstructGeomCamera(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomCamera *curves);

  bool ReconstructShader(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      Shader *shader);

  bool ReconstructNodeGraph(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      NodeGraph *graph);

  bool ReconstructMaterial(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      Material *material);

  bool ReconstructPreviewSurface(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      PreviewSurface *surface);

  bool ReconstructUVTexture(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      UVTexture *texture);

  bool ReconstructPrimvarReader_float2(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      PrimvarReader_float2 *reader_float2);

  bool ReconstructLuxSphereLight(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      LuxSphereLight *light);

  bool ReconstructLuxDomeLight(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      LuxDomeLight *light);

  bool ReconstructScope(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      Scope *scope);

  bool ReconstructSkelRoot(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      SkelRoot *skelroot);

  bool ReconstructSkeleton(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      Skeleton *skeleton);

  void ImportScene(tinyusdz::HighLevelScene &scene) { _imported_scene = scene; }

  bool HasPath(const std::string &path) {
    // TODO
    TokenizedPath tokPath(path);
    (void)tokPath;
    return false;
  }

  ///
  /// Reader entry point
  ///
  bool Read(LoadState state = LOAD_STATE_TOPLEVEL) {
    _sub_layered = (state == LOAD_STATE_SUBLAYER);
    _referenced = (state == LOAD_STATE_REFERENCE);
    _payloaded = (state == LOAD_STATE_PAYLOAD);

#if 0
    bool header_ok = ParseMagicHeader();
    if (!header_ok) {
      PushError("Failed to parse USDA magic header.\n");
      return false;
    }

    // global meta.
    bool has_meta = ParseWorldMeta();
    if (has_meta) {
      // TODO: Process meta info
    } else {
      // no meta info accepted.
    }

    // parse blocks
    while (!_sr->eof()) {
      if (!SkipCommentAndWhitespaceAndNewline()) {
        return false;
      }

      if (_sr->eof()) {
        // Whitespaces in the end of line.
        break;
      }

      // Look ahead token
      auto curr_loc = _sr->tell();

      std::string tok;
      if (!ReadToken(&tok)) {
        PushError("Token expected.\n");
        return false;
      }

      // Rewind
      if (!SeekTo(curr_loc)) {
        return false;
      }

      if (tok == "def") {
        bool block_ok = ParseDefBlock();
        if (!block_ok) {
          PushError("Failed to parse `def` block.\n");
          return false;
        }
      } else if (tok == "over") {
        bool block_ok = ParseOverBlock();
        if (!block_ok) {
          PushError("Failed to parse `over` block.\n");
          return false;
        }
      } else if (tok == "class") {
        bool block_ok = ParseClassBlock();
        if (!block_ok) {
          PushError("Failed to parse `class` block.\n");
          return false;
        }
      } else {
        PushError("Unknown token '" + tok + "'");
        return false;
      }
    }
    return true;
#else
    PUSH_ERROR_AND_RETURN("TODO:");
#endif

  }

  std::vector<GPrim> GetGPrims() { return _gprims; }

  std::string GetDefaultPrimName() const { return _defaultPrim; }

  std::string GetError() {
    return _err;
  }

  std::string GetWarning() {
    return _warn;
  }

 private:

  void RegisterNodeTypes() {
    _node_types.insert("Xform");
    _node_types.insert("Sphere");
    _node_types.insert("Cube");
    _node_types.insert("Cylinder");
    _node_types.insert("BasisCurves");
    _node_types.insert("Mesh");
    _node_types.insert("Scope");
    _node_types.insert("Material");
    _node_types.insert("NodeGraph");
    _node_types.insert("Shader");
    _node_types.insert("SphereLight");
    _node_types.insert("DomeLight");
    _node_types.insert("Camera");
    _node_types.insert("SkelRoot");
    _node_types.insert("Skeleton");
  }

  ///
  /// -- Members --
  ///

  std::set<std::string> _node_types;

  std::stack<ParseState> parse_stack;

  std::string _base_dir;  // Used for importing another USD file

  nonstd::optional<tinyusdz::HighLevelScene> _imported_scene;  // Imported scene.

  // "class" defs
  std::map<std::string, Klass> _klasses;

  std::stack<std::string> _path_stack;

  std::string _err;
  std::string _warn;

  // Cache of loaded `references`
  // <filename, {defaultPrim index, list of root nodes in referenced usd file}>
  std::map<std::string, std::pair<uint32_t, std::vector<GPrim>>>
      _reference_cache;

  // toplevel "def" defs
  std::vector<GPrim> _gprims;

  // load flags
  bool _sub_layered{false};
  bool _referenced{false};
  bool _payloaded{false};

  std::string _defaultPrim;

  ascii::AsciiParser _parser;

};  // namespace usda

// == impl ==
bool USDAReader::Impl::ReconstructGPrim(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references, GPrim *gprim) {
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend) {
    }
  }

  // Update props;
  for (auto item : properties) {
    if (item.second.is_rel) {
      PUSH_WARN("TODO: rel");
    } else {
      gprim->props[item.first].attrib = item.second.attrib;
    }
  }

  //
  // Resolve append references
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend) {
    }
  }

  return true;
}

static nonstd::expected<bool, std::string> CheckAllowedTypeOfXformOp(const PrimAttrib &attr, const std::vector<value::TypeId> &allowed_type_ids)
{
  for (size_t i = 0; i < allowed_type_ids.size(); i++) {
    if (attr.var.type_id() == allowed_type_ids[i]) {
      return true;
    }
  }

  std::stringstream ss;

  ss << "Allowed type for \"" << attr.name << "\"";
  if (allowed_type_ids.size() > 1) {
    ss << " are ";
  } else {
    ss << " is ";
  }

  for (size_t i = 0; i < allowed_type_ids.size(); i++) {
    ss << value::GetTypeName(allowed_type_ids[i]);
    if (i < (allowed_type_ids.size() - 1)) {
      ss << ", ";
    } else if (i == (allowed_type_ids.size() - 1)) {
      ss << " or ";
    }
  }
  ss << ", but got " << value::GetTypeName(attr.var.type_id());

  return nonstd::make_unexpected(ss.str());
}

bool USDAReader::Impl::ReconstructXform(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references, Xform *xform) {
  (void)xform;


  // ret = (basename, suffix, isTimeSampled?)
  auto Split =
      [](const std::string &str) -> std::tuple<std::string, std::string, bool> {
    bool isTimeSampled{false};

    std::string s = str;

    const std::string tsSuffix = ".timeSamples";

    if (endsWith(s, tsSuffix)) {
      isTimeSampled = true;
      // rtrim
      s = s.substr(0, s.size() - tsSuffix.size());

    }

    // TODO: Support multiple namespace(e.g. xformOp:translate:pivot)
    std::string suffix;
    if (s.find_last_of(':') != std::string::npos) {
      suffix = s.substr(s.find_last_of(':') + 1);
    }

    std::string basename = s;
    if (s.find_last_of(':') != std::string::npos) {
      basename = s.substr(0, s.find_last_of(':'));
    }

    return std::make_tuple(basename, suffix, isTimeSampled);
  };

  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend) {
    }
  }

  for (const auto &prop : properties) {
    if (startsWith(prop.first, "xformOp:translate")) {
      // TODO: Implement
      //using allowedTys = tinyusdz::variant<value::float3, value::double3>;
      std::vector<value::TypeId> ids;
      auto ret = CheckAllowedTypeOfXformOp(prop.second.attrib, ids);
      if (!ret) {

      }
    }
  }



  // Lookup xform values from `xformOpOrder`
  if (properties.count("xformOpOrder")) {

    // array of string
    auto prop = properties.at("xformOpOrder");
    if (prop.is_rel) {
      PUSH_WARN("TODO: Rel type for `xformOpOrder`");
    } else {
#if 0
      if (auto parr = value::as_vector<std::string>(&attrib->var)) {
        for (const auto &item : *parr) {
          // remove double-quotation
          std::string identifier = item;
          identifier.erase(
              std::remove(identifier.begin(), identifier.end(), '\"'),
              identifier.end());

          auto tup = Split(identifier);
          auto basename = std::get<0>(tup);
          auto suffix = std::get<1>(tup);
          auto isTimeSampled = std::get<2>(tup);
          (void)isTimeSampled;

          XformOp op;

          std::string target_name = basename;
          if (!suffix.empty()) {
            target_name += ":" + suffix;
          }

          if (!properties.count(target_name)) {
            PushError("Property '" + target_name +
                       "' not found in Xform node.");
            return false;
          }

          auto targetProp = properties.at(target_name);

          if (basename == "xformOp:rotateZ") {
            if (auto targetAttr = nonstd::get_if<PrimAttrib>(&targetProp)) {
              if (auto p = value::as_basic<float>(&targetAttr->var)) {
                std::cout << "xform got it "
                          << "\n";
                op.op = XformOp::OpType::ROTATE_Z;
                op.suffix = suffix;
                op.value = (*p);

                xform->xformOps.push_back(op);
              }
            }
          }
        }
      }
      PushError("`xformOpOrder` must be an array of string type.");
#endif
      (void)Split;
    }

  } else {
    //std::cout << "no xformOpOrder\n";
  }

  // For xformO
  // TinyUSDZ does no accept arbitrary types for variables with `xformOp` su
#if 0
    for (const auto &prop : properties) {


      if (prop.first == "xformOpOrder") {
        if (!prop.second.IsArray()) {
          PushError("`xformOpOrder` must be an array type.");
          return false;
        }

        for (const auto &item : prop.second.array) {
          if (auto p = nonstd::get_if<std::string>(&item)) {
            // TODO
            //XformOp op;
            //op.op =
          }
        }

      } else if (std::get<0>(tup) == "xformOp:rotateZ") {

        if (prop.second.IsTimeSampled()) {

        } else if (prop.second.IsFloat()) {
          if (auto p = nonstd::get_if<float>(&prop.second.value)) {
            XformOp op;
            op.op = XformOp::OpType::ROTATE_Z;
            op.precision = XformOp::PrecisionType::PRECISION_FLOAT;
            op.value = *p;

            std::cout << "rotateZ value = " << *p << "\n";

          } else {
            PushError("`xformOp:rotateZ` must be an float type.");
            return false;
          }
        } else {
          PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                     "\n");
        }

      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
#endif

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
    }
  }

  return true;
}

bool USDAReader::Impl::ReconstructGeomSphere(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomSphere *sphere) {
  (void)sphere;

  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {

    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          (void)prop;
#if 0
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference radius = " << (*p) << "\n";
                sphere->radius = *p;
              }
            }
          }
#endif
        }
      }
    }
  }

  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      // if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
      //   sphere->materialBinding.materialBinding = prel->path;
      // } else {
      //   PushError("`material:binding` must be 'rel' type.");
      //   return false;
      // }
    } else {
      if (prop.second.is_rel) {
        PUSH_WARN("TODO: Rel");
      } else {
        if (prop.first == "radius") {
          // const tinyusdz::PrimAttrib &attr = prop.second.attrib;
          // if (auto p = value::as_basic<double>(&attr->var)) {
          //   sphere->radius = *p;
          // } else {
          //   PushError("`radius` must be double type.");
          //   return false;
          // }
        } else {
          PUSH_ERROR_AND_RETURN("TODO: type: " + prop.first);
        }
      }
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          (void)prop;
          // if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
          //   if (prop.first == "radius") {
          //     if (auto p = value::as_basic<double>(&attr->var)) {
          //       SDCOUT << "append reference radius = " << (*p) << "\n";
          //       sphere->radius = *p;
          //     }
          //   }
          // }
        }
      }
    }
  }

  return true;
}

bool USDAReader::Impl::ReconstructGeomCone(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCone *cone) {
  (void)properties;
  (void)cone;
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {

    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          (void)prop;
#if 0
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference radius = " << (*p) << "\n";
                cone->radius = *p;
              }
            } else if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference height = " << (*p) << "\n";
                cone->height = *p;
              }
            }
          }
#endif
        }
      }
    }
  }

#if 0
  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
        cone->materialBinding.materialBinding = prel->path;
      } else {
        PushError("`material:binding` must be 'rel' type.");
        return false;
      }
    } else if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
      if (prop.first == "radius") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cone->radius = *p;
        } else {
          PushError("`radius` must be double type.");
          return false;
        }
      } else if (prop.first == "height") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cone->height = *p;
        } else {
          PushError("`height` must be double type.");
          return false;
        }
      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
  }
#endif

#if 0
  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference radius = " << (*p) << "\n";
                cone->radius = *p;
              }
            } else if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference height = " << (*p) << "\n";
                cone->height = *p;
              }
            }
          }
        }
      }
    }
  }
#endif

  return true;
}

bool USDAReader::Impl::ReconstructGeomCube(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCube *cube) {
  (void)properties;
  (void)cube;
#if 0
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {

    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "size") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference size = " << (*p) << "\n";
                cube->size = *p;
              }
            }
          }
        }
      }
    }
  }
#endif

#if 0
  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
        cube->materialBinding.materialBinding = prel->path;
      } else {
        PushError("`material:binding` must be 'rel' type.");
        return false;
      }
    } else if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
      if (prop.first == "size") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cube->size = *p;
        } else {
          PushError("`size` must be double type.");
          return false;
        }
      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
  }
#endif

#if 0
  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "size") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference size = " << (*p) << "\n";
                cube->size = *p;
              }
            }
          }
        }
      }
    }
  }
#endif

  return true;
}

bool USDAReader::Impl::ReconstructGeomCapsule(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCapsule *capsule) {
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {

    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (prop.second.is_rel) {
            PUSH_WARN("TODO: Rel");
          } else {
            // const PrimAttrib &attrib = prop.second.attrib;
#if 0
            if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference height = " << (*p) << "\n";
                capsule->height = *p;
              }
            } else if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference radius = " << (*p) << "\n";
                capsule->radius = *p;
              }
            } else if (prop.first == "axis") {
              if (auto p = value::as_basic<Token>(&attr->var)) {
                SDCOUT << "prepend reference axis = " << p->value << "\n";
                if (p->value == "x") {
                  capsule->axis = Axis::X;
                } else if (p->value == "y") {
                  capsule->axis = Axis::Y;
                } else if (p->value == "z") {
                  capsule->axis = Axis::Z;
                } else {
                  PUSH_WARN("Invalid axis token: " + p->value);
                }
              }
            }
#endif
          }
        }
      }
    }
  }

#if 0
  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
        capsule->materialBinding.materialBinding = prel->path;
      } else {
        PushError("`material:binding` must be 'rel' type.");
        return false;
      }
    } else if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
      if (prop.first == "height") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          capsule->height = *p;
        } else {
          PushError("`height` must be double type.");
          return false;
        }
      } else if (prop.first == "radius") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          capsule->radius = *p;
        } else {
          PushError("`radius` must be double type.");
          return false;
        }
      } else if (prop.first == "axis") {
        if (auto p = value::as_basic<Token>(&attr->var)) {
          if (p->value == "x") {
            capsule->axis = Axis::X;
          } else if (p->value == "y") {
            capsule->axis = Axis::Y;
          } else if (p->value == "z") {
            capsule->axis = Axis::Z;
          }
        } else {
          PushError("`axis` must be token type.");
          return false;
        }
      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference height = " << (*p) << "\n";
                capsule->height = *p;
              }
            } else if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference radius = " << (*p) << "\n";
                capsule->radius = *p;
              }
            } else if (prop.first == "axis") {
              if (auto p = value::as_basic<Token>(&attr->var)) {
                SDCOUT << "prepend reference axis = " << p->value << "\n";
                if (p->value == "x") {
                  capsule->axis = Axis::X;
                } else if (p->value == "y") {
                  capsule->axis = Axis::Y;
                } else if (p->value == "z") {
                  capsule->axis = Axis::Z;
                } else {
                  PUSH_WARN("Invalid axis token: " + p->value);
                }
              }
            }
          }
        }
      }
    }
  }
#endif

  return true;
}

bool USDAReader::Impl::ReconstructGeomCylinder(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCylinder *cylinder) {
#if 0
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {

    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference height = " << (*p) << "\n";
                cylinder->height = *p;
              }
            } else if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference radius = " << (*p) << "\n";
                cylinder->radius = *p;
              }
            } else if (prop.first == "axis") {
              if (auto p = value::as_basic<Token>(&attr->var)) {
                SDCOUT << "prepend reference axis = " << p->value << "\n";
                if (p->value == "x") {
                  cylinder->axis = Axis::X;
                } else if (p->value == "y") {
                  cylinder->axis = Axis::Y;
                } else if (p->value == "z") {
                  cylinder->axis = Axis::Z;
                } else {
                  PUSH_WARN("Invalid axis token: " + p->value);
                }
              }
            }
          }
        }
      }
    }
  }

  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
        cylinder->materialBinding.materialBinding = prel->path;
      } else {
        PushError("`material:binding` must be 'rel' type.");
        return false;
      }
    } else if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
      if (prop.first == "height") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cylinder->height = *p;
        } else {
          PushError("`height` must be double type.");
          return false;
        }
      } else if (prop.first == "radius") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cylinder->radius = *p;
        } else {
          PushError("`radius` must be double type.");
          return false;
        }
      } else if (prop.first == "axis") {
        if (auto p = value::as_basic<Token>(&attr->var)) {
          if (p->value == "x") {
            cylinder->axis = Axis::X;
          } else if (p->value == "y") {
            cylinder->axis = Axis::Y;
          } else if (p->value == "z") {
            cylinder->axis = Axis::Z;
          }
        } else {
          PushError("`axis` must be token type.");
          return false;
        }
      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference height = " << (*p) << "\n";
                cylinder->height = *p;
              }
            } else if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference radius = " << (*p) << "\n";
                cylinder->radius = *p;
              }
            } else if (prop.first == "axis") {
              if (auto p = value::as_basic<Token>(&attr->var)) {
                SDCOUT << "prepend reference axis = " << p->value << "\n";
                if (p->value == "x") {
                  cylinder->axis = Axis::X;
                } else if (p->value == "y") {
                  cylinder->axis = Axis::Y;
                } else if (p->value == "z") {
                  cylinder->axis = Axis::Z;
                } else {
                  PUSH_WARN("Invalid axis token: " + p->value);
                }
              }
            }
          }
        }
      }
    }
  }
#endif

  return true;
}

bool USDAReader::Impl::ReconstructGeomMesh(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomMesh *mesh) {
  //
  // Resolve prepend references
  //

  for (const auto &ref : references) {

    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      if (endsWith(asset_ref.asset_path, ".obj")) {
        std::string err;
        GPrim gprim;

        // abs path.
        std::string filepath = asset_ref.asset_path;

        if (io::IsAbsPath(asset_ref.asset_path)) {
          // do nothing
        } else {
          if (!_base_dir.empty()) {
            filepath = io::JoinPath(_base_dir, filepath);
          }
        }

        DCOUT("Reading .obj file: " + filepath);

        if (!usdObj::ReadObjFromFile(filepath, &gprim, &err)) {
          PUSH_ERROR_AND_RETURN("Failed to read .obj(usdObj). err = " + err);
        }
        DCOUT("Loaded .obj file: " + filepath);

        mesh->visibility = gprim.visibility;
        mesh->doubleSided = gprim.doubleSided;
        mesh->orientation = gprim.orientation;

        if (gprim.props.count("points")) {
          DCOUT("points");
          const Property &prop = gprim.props.at("points");
          if (prop.is_rel) {
            PUSH_WARN("TODO: points Rel\n");
          } else {
            const PrimAttrib &attr = prop.attrib;
            // PrimVar
            DCOUT("points.type:" + attr.var.type_name());
            if (attr.var.is_scalar()) {
              auto p = attr.var.get_value<std::vector<value::point3f>>();
              if (p) {
                mesh->points = p.value();
              } else {
                PUSH_ERROR_AND_RETURN("TODO: points.type = " +
                                      attr.var.type_name());
              }
              // if (auto p = value::as_vector<value::float3>(&pattr->var)) {
              //   DCOUT("points. sz = " + std::to_string(p->size()));
              //   mesh->points = (*p);
              // }
            } else {
              PUSH_ERROR_AND_RETURN("TODO: timesample points.");
            }
          }
        }

      } else {
        DCOUT("Not a .obj file");
      }
    }
  }

  for (const auto &prop : properties) {
    if (prop.second.is_rel) {
      if (prop.first == "material:binding") {
        mesh->materialBinding.materialBinding = prop.second.rel.path;
      } else {
        PUSH_WARN("TODO: rel");
      }
    } else {
      const PrimAttrib &attr = prop.second.attrib;
      if (prop.first == "points") {
        auto p = attr.var.get_value<std::vector<value::point3f>>();
        if (p) {
          mesh->points = (*p);
        } else {
          PUSH_ERROR_AND_RETURN(
              "`GeomMesh::points` must be point3[] type, but got " +
              attr.var.type_name());
        }
      } else if (prop.first == "subdivisionScheme") {
        auto p = attr.var.get_value<std::string>();
        if (!p) {
          PUSH_ERROR_AND_RETURN(
              "Invalid type for \'subdivisionScheme\'. expected \'STRING\' but "
              "got " +
              attr.var.type_name());
        } else {
          DCOUT("subdivisionScheme = " + (*p));
          if (p->compare("none") == 0) {
            mesh->subdivisionScheme = SubdivisionScheme::None;
          } else if (p->compare("catmullClark") == 0) {
            mesh->subdivisionScheme = SubdivisionScheme::CatmullClark;
          } else if (p->compare("bilinear") == 0) {
            mesh->subdivisionScheme = SubdivisionScheme::Bilinear;
          } else if (p->compare("loop") == 0) {
            mesh->subdivisionScheme = SubdivisionScheme::Loop;
          } else {
            PUSH_ERROR_AND_RETURN("Unknown subdivision scheme: " + (*p));
          }
        }
      } else {
        PUSH_WARN(" TODO: prop: " + prop.first);
      }
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      // TODO
    }
  }

  return true;
}

bool USDAReader::Impl::ReconstructBasisCurves(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomBasisCurves *curves) {
  for (const auto &prop : properties) {
    if (prop.first == "points") {
      if (prop.second.is_rel) {
        PUSH_WARN("TODO: Rel");
      } else {
        // const PrimAttrib &attrib = prop.second.attrib;
#if 0  // TODO
        attrib.
        attrib.IsFloat3() && !prop.second.IsArray()) {
        PushError("`points` must be float3 array type.");
        return false;
      }

      const std::vector<float3> p =
          nonstd::get<std::vector<float3>>(prop.second.value);

      curves->points.resize(p.size() * 3);
      memcpy(curves->points.data(), p.data(), p.size() * 3);
#endif
      }

    } else if (prop.first == "curveVertexCounts") {
#if 0  // TODO
      if (!prop.second.IsInt() && !prop.second.IsArray()) {
        PushError("`curveVertexCounts` must be int array type.");
        return false;
      }

      const std::vector<int32_t> p =
          nonstd::get<std::vector<int32_t>>(prop.second.value);

      curves->curveVertexCounts.resize(p.size());
      memcpy(curves->curveVertexCounts.data(), p.data(), p.size());
#endif

    } else {
      PUSH_WARN("TODO: " << prop.first);
    }
  }

  return true;
}

bool USDAReader::Impl::ReconstructGeomCamera(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCamera *camera) {
  for (const auto &prop : properties) {
    if (prop.first == "focalLength") {
      // TODO
    } else {
      //std::cout << "TODO: " << prop.first << "\n";
    }
  }

  return true;
}

bool USDAReader::Impl::ReconstructLuxSphereLight(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    LuxSphereLight *light) {
  // TODO: Implement
  for (const auto &prop : properties) {
    if (prop.first == "radius") {
      // TODO
    } else {
      //std::cout << "TODO: " << prop.first << "\n";
    }
  }

  return true;
}

#define PARSE_PROPERTY_BEGIN if (0) {
// TODO(syoyo): TimeSamples, Reference
#define PARSE_PROPERTY(__prop, __name, __ty, __target)             \
  }                                                                \
  else if (__prop.first == __name) {                               \
    const PrimAttrib &attr = __prop.second.attrib;                 \
    if (auto v = attr.var.get_value<__ty>()) {                     \
      __target = v.value();                                        \
    } else {                                                       \
      PUSH_ERROR_AND_RETURN("Type mismatch. "                      \
                            << __name << " expects "               \
                            << value::TypeTrait<__ty>::type_name); \
    }

//#define PARSE_PROPERTY_END }

bool USDAReader::Impl::ReconstructLuxDomeLight(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    LuxDomeLight *light) {
  // TODO: Implement
  for (const auto &prop : properties) {
    PARSE_PROPERTY_BEGIN
    PARSE_PROPERTY(prop, "guideRadius", float, light->guideRadius)
    PARSE_PROPERTY(prop, "inputs:color", value::color3f, light->color)
    PARSE_PROPERTY(prop, "inputs:intensity", float, light->intensity)
  }
  else {
    DCOUT("TODO: " << prop.first);
  }
}

return true;
}  // namespace usda

bool USDAReader::Impl::ReconstructScope(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references, Scope *scope) {
  (void)scope;

  // TODO: Implement
  DCOUT("Implement Scope");

  return true;
}

bool USDAReader::Impl::ReconstructSkelRoot(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    SkelRoot *root) {
  (void)root;

  // TODO: Implement
  DCOUT("Implement SkelRoot");

  return true;
}

bool USDAReader::Impl::ReconstructSkeleton(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    Skeleton *skel) {
  (void)skel;

  // TODO: Implement
  DCOUT("Implement Skeleton");

  return true;
}

bool USDAReader::Impl::ReconstructShader(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    Shader *shader) {
  for (const auto &prop : properties) {
    if (prop.first == "info:id") {
      const PrimAttrib &attr = prop.second.attrib;

      auto p = attr.var.get_value<std::string>();
      if (p) {
        if (p->compare("UsdPreviewSurface") == 0) {
          PreviewSurface surface;
          if (!ReconstructPreviewSurface(properties, references, &surface)) {
            PUSH_WARN("TODO: reconstruct PreviewSurface.");
          }
          shader->value = surface;
        } else if (p->compare("UsdUVTexture") == 0) {
          UVTexture texture;
          if (!ReconstructUVTexture(properties, references, &texture)) {
            PUSH_WARN("TODO: reconstruct UVTexture.");
          }
          shader->value = texture;
        } else if (p->compare("UsdPrimvarReader_float2") == 0) {
          PrimvarReader_float2 preader;
          if (!ReconstructPrimvarReader_float2(properties, references,
                                               &preader)) {
            PUSH_WARN(
                "TODO: reconstruct PrimvarReader_float2.");
          }
          shader->value = preader;
        } else {
          PUSH_ERROR_AND_RETURN("Invalid or Unsupported Shader id: " + (*p));
        }
      }
    } else {
      //std::cout << "TODO: " << prop.first << "\n";
    }
  }

  return true;
}

bool USDAReader::Impl::ReconstructNodeGraph(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    NodeGraph *graph) {

  (void)properties;
  (void)references;
  (void)graph;

  PUSH_WARN("TODO: reconstruct NodeGrah.");

  return true;
}

bool USDAReader::Impl::ReconstructMaterial(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    Material *material) {
  (void)properties;
  (void)references;
  (void)material;

  PUSH_WARN("TODO: Implement Material.");

  return true;
}

bool USDAReader::Impl::ReconstructPreviewSurface(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    PreviewSurface *surface) {
  // TODO:
  return false;
}

bool USDAReader::Impl::ReconstructUVTexture(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    UVTexture *texture) {
  // TODO:
  return false;
}

bool USDAReader::Impl::ReconstructPrimvarReader_float2(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    PrimvarReader_float2 *preader) {
  // TODO:
  return false;
}

//
// --
//

bool IsUSDA(const std::string &filename, size_t max_filesize) {
  // TODO: Read only first N bytes
  std::vector<uint8_t> data;
  std::string err;

  if (!io::ReadWholeFile(&data, &err, filename, max_filesize)) {
    return false;
  }

  tinyusdz::StreamReader sr(data.data(), data.size(), /* swap endian */ false);
  tinyusdz::ascii::AsciiParser parser(&sr);

  return parser.CheckHeader();
}

///
/// -- USDAReader
///
USDAReader::USDAReader(StreamReader *sr) { _impl = new Impl(sr); }

USDAReader::~USDAReader() { delete _impl; }

bool USDAReader::Read(LoadState state) { return _impl->Read(state); }

void USDAReader::SetBaseDir(const std::string &dir) {
  return _impl->SetBaseDir(dir);
}

std::vector<GPrim> USDAReader::GetGPrims() { return _impl->GetGPrims(); }

std::string USDAReader::GetDefaultPrimName() const {
  return _impl->GetDefaultPrimName();
}

std::string USDAReader::GetError() { return _impl->GetError(); }
std::string USDAReader::GetWarning() { return _impl->GetWarning(); }

}  // namespace usda
}  // namespace tinyusdz


#else

namespace tinyusdz {
namespace usda {

USDAReader::USDAReader(StreamReader *sr) { (void)sr; }

USDAReader::~USDAReader() { }

bool USDAReader::CheckHeader() { return false; }

bool USDAReader::Parse(LoadState state) { (void)state; return false; }

void USDAReader::SetBaseDir(const std::string &dir) {
  (void)dir;
}

std::vector<GPrim> USDAReader::GetGPrims() {
  return {};
}

std::string USDAReader::GetDefaultPrimName() const {
  return std::string{};
}

std::string USDAReader::GetError() { return "USDA parser feature is disabled in this build.\n"; }
std::string USDAReader::GetWarning() { return std::string{}; }

}  // namespace usda
}  // namespace tinyusdz

#endif
