## TODO: Struct Field Assignment Operator Migration

Currently, struct field assignment is handled using the `=` operator (e.g., `(= (. target :field) value)`). This does not enforce explicit pointer semantics and can lead to ambiguity in code generation. We plan to migrate struct field assignment to the `:=` operator, so that all field assignments require explicit pointer assignment, improving clarity and correctness in the IR.
# Finna Yeet on Em

## Install
```sh
cmake --preset=default
cmake --build build
```

## TODO Laundry List
* EDN comments aren't working
* Floating point math is erroring

* Cleanup entry function logic to avoid assuming the last operation is the return value
* Add an explicit return operator to Yeet, so return values are not inferred from the last expression

### CMakeUserPresets.json
```json
{
  "version": 2,
  "configurePresets": [
    {
      "name": "darwin-debug",
      "inherits": "ninja",
      "environment": {
        "VCPKG_ROOT": "/Users/clarkrinker/src/github.com/microsoft/vcpkg",
        "CMAKE_BUILD_TYPE": "Debug"
      }
    }
  ]
}
```