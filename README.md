# Finna Yeet on Em

## TODO Laundry List
* EDN comments aren't working

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