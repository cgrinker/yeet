# Finna Yeet on Em

## Install
```sh
cmake --preset=default
cmake --build build
```

## TODO Laundry List
* EDN comments aren't working
* Floating point math is erroring

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