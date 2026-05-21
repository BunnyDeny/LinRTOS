#!/usr/bin/env python3
"""
Load a defconfig (minimal diff) into .config (full config),
then generate build/linrtos_kconfig.h automatically.
Usage: python3 tools/linrtos_load_defconfig.py <defconfig_path>
"""
import sys
import os

# Allow importing sibling script
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    from kconfiglib import Kconfig
except ImportError:
    print("Error: kconfiglib is not installed.")
    print("Please run:  sudo apt install python3-kconfiglib")
    sys.exit(1)

import config_to_header


def main():
    if len(sys.argv) != 2:
        print("Usage: linrtos_load_defconfig.py <defconfig_path>")
        sys.exit(1)

    defconfig_path = sys.argv[1]
    if not os.path.exists(defconfig_path):
        print(f"Error: {defconfig_path} not found")
        sys.exit(1)

    kconfig = Kconfig("Kconfig")
    # load_config with replace=True resets all symbols to defaults,
    # then applies the values from the defconfig file.
    kconfig.load_config(defconfig_path)
    kconfig.write_config(".config")
    print(f"Loaded {defconfig_path} into .config")

    # Auto-generate header to root build/ directory
    os.makedirs("build", exist_ok=True)
    config_to_header.convert(".config", "build/linrtos_kconfig.h")

    # Auto-generate header for stm32g431 example
    config_to_header.convert(".config", "examples/stm32g431/build/linrtos_kconfig.h")


if __name__ == "__main__":
    main()
