
import argparse
import subprocess
import os
import sys

def find_project_root():
    cur = os.path.abspath(os.path.dirname(__file__))
    while True:
        lock_path = os.path.join(cur, 'yeetroot.lock')
        if os.path.isfile(lock_path):
            return cur
        parent = os.path.dirname(cur)
        if parent == cur:
            return None
        cur = parent

project_root = find_project_root()
if not project_root:
    print("Error: yeetroot.lock not found. Run this script from within a yeet project.")
    sys.exit(1)
os.chdir(project_root)


def makePreset():
    if sys.platform.startswith("win"):
        configure_preset = "windows-vcpkg"
    else:
        configure_preset = "ninja-vcpkg"

    if not "VCPKG_ROOT" in os.environ:
        raise ValueError("VCPKG_ROOT environment variable is not set. Please set it to your vcpkg root directory.")
    presets_content = f'''{{
  "version": 2,
  "userPresets": [
    {{
      "name": "default",
      "configurePreset": "{configure_preset}",
      "buildPreset": "default",
      "environment": {{
        "VCPKG_ROOT": {os.environ["VCPKG_ROOT"]},
        "CMAKE_BUILD_TYPE": "Debug"
      }}
    }}
  ]
}}'''
    return presets_content

def init():
    preset_path = os.path.join(project_root, "CMakeUserPresets.json")
    if os.path.exists(preset_path):
        print("CMakeUserPresets.json already exists. Exiting.")
        return
    presets_content = makePreset()
    with open(preset_path, "w") as f:
        f.write(presets_content)
    print("CMakeUserPresets.json initialized.")
    print("\nInstall instructions:")
    print("""sh\ncmake --preset=default\ncmake --build build\n""")

def test():
    sexpr_dir = os.path.abspath(os.path.join(project_root, 'sexpr'))
    main_exe = os.path.abspath(os.path.join(project_root, 'build', 'main'))
    for fname in os.listdir(sexpr_dir):
        if fname.endswith('.yeet'):
            fpath = os.path.join(sexpr_dir, fname)
            print(f"Running test: {fname}")
            try:
                result = subprocess.run([main_exe, "--filename=" + fpath], capture_output=True, text=True)
                print(result.stdout)
                if result.stderr:
                    print(result.stderr)
            except Exception as e:
                print(f"Error running {fname}: {e}")

def clean():
    build_dir = os.path.join(project_root, 'build')
    if os.path.isdir(build_dir):
        import shutil
        shutil.rmtree(build_dir)
        print(f"Removed build directory: {build_dir}")
    else:
        print("No build directory found.")


def main():
    parser = argparse.ArgumentParser(description="Yeet Project Helpers")
    subparsers = parser.add_subparsers(dest="command")

    parser_init = subparsers.add_parser("init", help="Project setup")
    parser_test = subparsers.add_parser("test", help="Run all .yeet sexpr tests")
    parser_clean = subparsers.add_parser("clean", help="Remove build directory")

    args = parser.parse_args()
    if args.command == "init":
        init()
    elif args.command == "test":
        test()
    elif args.command == "clean":
        clean()
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
