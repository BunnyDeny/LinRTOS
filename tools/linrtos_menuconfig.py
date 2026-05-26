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
    if os.path.exists(dotconfig):
        kconfig.load_config(dotconfig)
        print(f"Loaded existing config: {dotconfig}")
        config_mtime_before = os.path.getmtime(dotconfig)
    else:
        print("No existing .config found, using defaults.")
        config_mtime_before = None

    # Launch interactive TUI
    menuconfig(kconfig)

    # Determine whether the user saved the configuration by checking
    # if .config was modified during menuconfig execution.
    if os.path.exists(dotconfig):
        config_mtime_after = os.path.getmtime(dotconfig)
    else:
        config_mtime_after = None

    if config_mtime_after != config_mtime_before:
        # User saved the configuration. Sync the generated header.
        import config_to_header
        config_to_header.convert(dotconfig, "include/linrtos_kconfig.h")
    else:
        print(f"Configuration changes discarded (not saved to {dotconfig}).")


if __name__ == "__main__":
    main()
