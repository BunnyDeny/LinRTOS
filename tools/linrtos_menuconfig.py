#!/usr/bin/env python3
"""
LinRTOS menuconfig frontend.
Requires: apt install python3-kconfiglib  (or: pip3 install kconfiglib)
"""
import os
import sys

# Let script give a friendly hint if kconfiglib is not installed
try:
    from kconfiglib import Kconfig
    from menuconfig import menuconfig
except ImportError as e:
    print("Error: kconfiglib or menuconfig is not installed.")
    print("Please run one of the following:")
    print("  sudo apt install python3-kconfiglib")
    print("  pip3 install kconfiglib")
    sys.exit(1)


def main():
    # Use Linux-kernel-style blue background + white highlight theme
    os.environ["MENUCONFIG_STYLE"] = (
        "aquatic "
        "list=fg:white,bg:blue "
        "selection=fg:black,bg:white,bold "
        "inv-list=fg:white,bg:blue "
        "inv-selection=fg:black,bg:white "
        "show-help=fg:white,bg:blue "
        "text=fg:white,bg:blue"
    )

    # Parse from project root Kconfig
    kconfig = Kconfig("Kconfig")

    dotconfig = ".config"
    old_content = ""
    if os.path.exists(dotconfig):
        with open(dotconfig, "r", encoding="utf-8") as f:
            old_content = f.read()
        kconfig.load_config(dotconfig)
        print(f"Loaded existing config: {dotconfig}")
    else:
        print("No existing .config found, using defaults.")

    # Launch interactive TUI
    menuconfig(kconfig)

    # Write config (kconfiglib avoids touching the file if content is identical)
    kconfig.write_config(dotconfig)

    # Check if content actually changed for user feedback
    new_content = ""
    if os.path.exists(dotconfig):
        with open(dotconfig, "r", encoding="utf-8") as f:
            new_content = f.read()

    if old_content != new_content:
        print(f"Configuration saved to {dotconfig}")


if __name__ == "__main__":
    main()
