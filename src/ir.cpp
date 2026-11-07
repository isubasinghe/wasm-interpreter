#include "ir.h"

#include <unordered_map>
#include <utility>

#include "errors.h"

const char* wasm_type_string(wasm_type_t type) {
  switch (type) {
    case WASM_TYPE_I32:       return "i32"; break;
    case WASM_TYPE_I64:       return "i64"; break;
    case WASM_TYPE_F32:       return "f32"; break;
    case WASM_TYPE_F64:       return "f64"; break;
    case WASM_TYPE_V128:      return "v128"; break;
    case WASM_TYPE_EXTERNREF: return "externref"; break;
    case WASM_TYPE_FUNCREF:   return "funcref"; break;
    default:  
      return "unknown";
  }
}

const char* wasm_kind_string(wasm_kind_t kind) {
  switch (kind) {
    case KIND_FUNC:     return "func"; break;
    case KIND_TABLE:    return "table"; break;
    case KIND_MEMORY:   return "memory"; break;
    case KIND_GLOBAL:   return "global"; break;
    default:  
      return "unknown";
  }
}

// Returns the string name of a section code.
const char* wasm_section_name(byte code) {
  switch (code) {
  case WASM_SECT_TYPE: return "type";
  case WASM_SECT_IMPORT: return "import";
  case WASM_SECT_FUNCTION: return "function";
  case WASM_SECT_TABLE: return "table";
  case WASM_SECT_MEMORY: return "memory";
  case WASM_SECT_GLOBAL: return "global";
  case WASM_SECT_EXPORT: return "export";
  case WASM_SECT_START: return "start";
  case WASM_SECT_ELEMENT: return "element";
  case WASM_SECT_CODE: return "code";
  case WASM_SECT_DATA: return "data";
  case WASM_SECT_DATACOUNT: return "datacount";
  case WASM_SECT_CUSTOM: return "custom";
  default:
    return "unknown";
  }
}


namespace {

using CopyMap = std::unordered_map<const void*, void*>;

template<typename Container>
void map_copies(const Container &source, Container &destination, CopyMap &copies) {
  auto dest = destination.begin();
  for(const auto &value: source) {
    copies.emplace(&value, &*dest++);
  }
}

template<typename T>
T *copied_pointer(T *source, const CopyMap &copies) {
  if(source == nullptr) {
    return nullptr;
  }
  auto copy = copies.find(source);
  if(copy == copies.end()) {
    throw WasmValidationError("module reference points outside its owned data");
  }
  return static_cast<T*>(copy->second);
}

template<typename T>
void patch_descriptors(std::list<T> &descriptors, const CopyMap &copies) {
  for(auto &value: descriptors) {
    switch(value.kind) {
      case KIND_FUNC: value.desc.func = copied_pointer(value.desc.func, copies); break;
      case KIND_TABLE: value.desc.table = copied_pointer(value.desc.table, copies); break;
      case KIND_MEMORY: value.desc.mem = copied_pointer(value.desc.mem, copies); break;
      case KIND_GLOBAL: value.desc.global = copied_pointer(value.desc.global, copies); break;
      default:
        throw WasmValidationError("invalid import/export kind");
    }
  }
}

} // namespace

WasmModule::WasmModule(const WasmModule &mod)
    : magic(mod.magic), version(mod.version), customs(mod.customs), sigs(mod.sigs),
      imports(mod.imports), funcs(mod.funcs), tables(mod.tables), mems(mod.mems),
      globals(mod.globals), exports(mod.exports), elems(mod.elems), datas(mod.datas),
      has_datacount(mod.has_datacount), num_datas_datacount(mod.num_datas_datacount) {
  // Copy containers first, then redirect every internal reference into the copy.
  CopyMap copies;
  map_copies(mod.sigs, sigs, copies);
  map_copies(mod.funcs, funcs, copies);
  map_copies(mod.tables, tables, copies);
  map_copies(mod.mems, mems, copies);
  map_copies(mod.globals, globals, copies);

  for(auto &func: funcs) {
    func.sig = copied_pointer(func.sig, copies);
  }
  patch_descriptors(imports.list, copies);
  patch_descriptors(exports, copies);
  start_fn = copied_pointer(mod.start_fn, copies);

  for(auto &elem: elems) {
    for(auto &func: elem.func_indices) {
      func = copied_pointer(func, copies);
    }
  }
  for(auto &data: datas) {
    data.mem = copied_pointer(data.mem, copies);
  }

  auto source_custom = mod.customs.begin();
  for(auto &custom: customs) {
    copies.emplace(&source_custom->debug.func_assoc, &custom.debug.func_assoc);
    for(auto &name: custom.debug.func_assoc) {
      name.func = copied_pointer(name.func, copies);
    }
    ++source_custom;
  }
  fn_names_debug = copied_pointer(mod.fn_names_debug, copies);
}

void WasmModule::swap(WasmModule &other) noexcept {
  using std::swap;
  swap(magic, other.magic);
  swap(version, other.version);
  customs.swap(other.customs);
  sigs.swap(other.sigs);
  imports.list.swap(other.imports.list);
  swap(imports.num_funcs, other.imports.num_funcs);
  swap(imports.num_tables, other.imports.num_tables);
  swap(imports.num_mems, other.imports.num_mems);
  swap(imports.num_globals, other.imports.num_globals);
  funcs.swap(other.funcs);
  tables.swap(other.tables);
  mems.swap(other.mems);
  globals.swap(other.globals);
  exports.swap(other.exports);
  elems.swap(other.elems);
  datas.swap(other.datas);
  swap(start_fn, other.start_fn);
  swap(has_datacount, other.has_datacount);
  swap(num_datas_datacount, other.num_datas_datacount);
  swap(fn_names_debug, other.fn_names_debug);
}

WasmModule& WasmModule::operator=(const WasmModule &mod) {
  if(this != &mod) {
    // Keep the original module intact if allocating or patching the copy fails.
    WasmModule copy(mod);
    swap(copy);
  }
  return *this;
}
