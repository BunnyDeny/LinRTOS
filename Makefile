.PHONY: all menuconfig oldconfig defconfig savedefconfig mrproper help build-g431 flash-g431

# Default target: show help
all: help

help:
	@echo "LinRTOS Kconfig Build System"
	@echo ""
	@echo "  make menuconfig      - Interactive configuration (TUI)"
	@echo "  make oldconfig       - Update .config with defaults for new symbols"
	@echo "  make defconfig       - Load default config (configs/LinRTOS_defconfig)"
	@echo "  make xxx_defconfig   - Load configs/xxx_defconfig and generate header"
	@echo "  make savedefconfig   - Save minimal config to configs/LinRTOS_defconfig"
	@echo "  make build-g431      - Build stm32g431 example project"
	@echo "  make flash-g431      - Build and flash stm32g431 via OpenOCD (CMSIS-DAP)"
	@echo "  make mrproper        - Remove .config, generated headers, and example build/"

# Kconfig / menuconfig targets
menuconfig:
	@python3 tools/linrtos_menuconfig.py

oldconfig:
	@python3 tools/linrtos_oldconfig.py

defconfig:
	@python3 tools/linrtos_load_defconfig.py configs/LinRTOS_defconfig

# Support make xxx_defconfig: auto-lookup configs/ directory
%_defconfig:
	@if [ -f "configs/$@" ]; then \
		python3 tools/linrtos_load_defconfig.py configs/$@; \
	else \
		echo "Error: configs/$@ not found"; \
		exit 1; \
	fi

savedefconfig:
	@python3 -c "from kconfiglib import Kconfig; k=Kconfig('Kconfig'); k.load_config('.config'); k.write_min_config('configs/LinRTOS_defconfig')"
	@echo "Saved minimal defconfig to configs/LinRTOS_defconfig"

mrproper:
	@rm -f .config .config.old
	@rm -f include/linrtos_kconfig.h
	@rm -rf examples/stm32g431/build
	@echo "Cleaned .config, generated headers, and example build/"

build-g431:
	@if [ ! -f "include/linrtos_kconfig.h" ]; then \
		echo "linrtos_kconfig.h not found, running 'make defconfig'..."; \
		$(MAKE) defconfig; \
	fi
	@$(MAKE) -C examples/stm32g431 all

flash-g431: build-g431
	@cd examples/stm32g431 && openocd -f interface/cmsis-dap.cfg -f target/stm32g4x.cfg -c "program ./build/stm32g431_gcc_example_project.elf verify reset exit"
