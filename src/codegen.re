open Modulegen.BsDecl;

open Modulegen.BsType;

exception CodegenTypeError string;

exception CodegenConstructorError string;

module Utils = {
  let unquote str => String.sub str 1 (String.length str - 2);
  let normalize_name =
    String.map (
      fun ch =>
        if (ch == '-') {
          '_'
        } else {
          ch
        }
    );
  let to_module_name str => normalize_name (unquote str);
  let rec uniq =
    fun
    | [] => []
    | [h, ...t] => {
        let no_dups = uniq (List.filter (fun x => x != h) t);
        [h, ...no_dups]
      };
  let is_optional (_, type_of) =>
    switch type_of {
    | Optional _ => true
    | _ => false
    };
};

let rec bstype_name =
  fun
  | Regex => "regex"
  | Unit => "unit"
  | Null => "null"
  | Any => "any"
  | Object _ => "object"
  | Number => "number"
  | Dict t => "dict_" ^ bstype_name t
  | String => "string"
  | Boolean => "bool"
  | Function _ => "func"
  | Unknown => "unknown"
  | Array t => "array_" ^ bstype_name t
  | Tuple types => "tuple_of_" ^ (List.map bstype_name types |> String.concat "_")
  | Named s => String.uncapitalize_ascii s
  | Union types => union_types_to_name types
  | Class props => raise (CodegenTypeError "Unable to translate class into type name")
  | Optional t => ""
and union_types_to_name types => {
  let type_names = List.map bstype_name types;
  String.concat "_or_" type_names
};

let rec bstype_to_code =
  fun
  | Regex => "Js.Re.t"
  | Dict t => "Js.Dict.t (" ^ bstype_to_code t ^ ")"
  | Optional t => bstype_to_code t ^ "?"
  | Unit => "unit"
  | Null => "null"
  | Array t => "array " ^ bstype_to_code t
  | Tuple types => <Render.TupleType types=(List.map bstype_to_code types) />
  | Unknown => "??"
  | Any => "_"
  | Object props =>
    <Render.ObjectType
      statements=(List.map (fun (key, type_of) => (key, bstype_to_code type_of)) props)
    />
  | Number => "float"
  | String => "string"
  | Boolean => "Js.boolean"
  | Named s => String.uncapitalize_ascii s
  | Union types => union_types_to_name types
  | Function params rt =>
    <Render.FunctionType
      params=(List.map (fun (name, param) => (name, bstype_to_code param)) params)
      has_optional=(List.exists Utils.is_optional params)
      return_type=(bstype_to_code rt)
    />
  | Class props =>
    "Js.t {. " ^
    String.concat
      ", "
      (
        List.filter (fun (key, type_of) => key != "constructor") props |>
        List.map (
          fun (key, type_of) =>
            key ^
            ": " ^
            bstype_to_code type_of ^ (
              switch type_of {
              | Function _ => " [@bs.meth]"
              | _ => ""
              }
            )
        )
      ) ^ " }";

module Precode = {
  let rec bstype_precode def =>
    switch def {
    | Union types => [string_of_union_types def types]
    | Function params rt => List.map (fun (id, t) => bstype_precode t) params |> List.flatten
    | Object types => List.map (fun (id, type_of) => bstype_precode type_of) types |> List.flatten
    | Class types => List.map (fun (id, type_of) => bstype_precode type_of) types |> List.flatten
    | Optional t => bstype_precode t
    | Array t => bstype_precode t
    | Dict t => bstype_precode t
    | _ => [""]
    }
  and string_of_union_types t types =>
    "type " ^
    bstype_name t ^
    " = " ^
    String.concat
      ""
      (
        List.map
          (
            fun union_type =>
              "\n| " ^
              String.capitalize_ascii (bstype_name union_type) ^
              " (" ^ bstype_to_code union_type ^ ")"
          )
          types
      ) ^ ";\n";
  let decl_to_precode =
    fun
    | VarDecl _ type_of => bstype_precode type_of
    | FuncDecl _ type_of => bstype_precode type_of
    | TypeDecl id type_of =>
      bstype_precode type_of |>
      List.cons ("type " ^ String.uncapitalize_ascii id ^ " = " ^ bstype_to_code type_of ^ ";")
    | ClassDecl _ type_of => bstype_precode type_of
    | ExportsDecl type_of => bstype_precode type_of
    | _ => [""];
  let from_stack stack =>
    switch stack {
    | ModuleDecl id statements =>
      List.map decl_to_precode statements |> List.flatten |> Utils.uniq |> String.concat "\n"
    | TypeDecl _ type_of => ""
    | _ => ""
    };
};

let constructor_type class_name =>
  fun
  | Class props => {
      let constructors = List.find_all (fun (id, _) => id == "constructor") props;
      if (List.length constructors == 0) {
        bstype_to_code (Function [("_", Unit)] (Named class_name))
      } else {
        let (_, cons_type) = List.hd constructors;
        bstype_to_code cons_type
      }
    }
  | _ => raise (CodegenConstructorError "Type has no constructor");

let rec declaration_to_code module_id =>
  fun
  | VarDecl id type_of =>
    <Render.VariableDeclaration
      name=(Utils.normalize_name id)
      module_id=(Utils.unquote module_id)
      type_of=(bstype_to_code type_of)
    />
  | FuncDecl id type_of =>
    <Render.VariableDeclaration
      name=(Utils.normalize_name id)
      module_id=(Utils.unquote module_id)
      type_of=(bstype_to_code type_of)
    />
  | ExportsDecl type_of =>
    <Render.VariableDeclaration
      name=(Utils.normalize_name module_id)
      type_of=(bstype_to_code type_of)
      module_id=(Utils.unquote module_id)
      is_exports=true
    />
  | ModuleDecl id statements =>
    <Render.ModuleDeclaration name=id statements=(List.map (declaration_to_code id) statements) />
  | TypeDecl id type_of => ""
  | ClassDecl id type_of => {
      let class_name = String.uncapitalize_ascii id;
      let ctor_type = constructor_type class_name type_of;
      let class_type = bstype_to_code type_of;
      <Render.ClassDeclaration
        name=class_name
        exported_as=id
        module_id=(Utils.unquote module_id)
        class_type
        ctor_type
      />
    }
  | Unknown => "??;";

let stack_to_code stack =>
  switch stack {
  | ModuleDecl id statements =>
    Some (
      Utils.to_module_name id,
      Precode.from_stack stack ^ String.concat "\n" (List.map (declaration_to_code id) statements)
    )
  | TypeDecl _ _ => Some ("", Precode.from_stack stack ^ declaration_to_code "" stack)
  | _ => None
  };
