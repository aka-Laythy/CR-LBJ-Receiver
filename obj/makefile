################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################
-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include User/subdir.mk
-include Startup/subdir.mk
-include Peripheral/src/subdir.mk
-include Debug/subdir.mk
-include Core/subdir.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
SECONDARY_FLASH += \
0000----CH32V005F6P6-power-board-ble-pmos.hex \

SECONDARY_LIST += \
0000----CH32V005F6P6-power-board-ble-pmos.lst \

SECONDARY_SIZE += \
0000----CH32V005F6P6-power-board-ble-pmos.siz \


# All Target
all: 
	$(MAKE) --no-print-directory main-build 

main-build: 0000----CH32V005F6P6-power-board-ble-pmos.elf secondary-outputs

# Tool invocations
0000----CH32V005F6P6-power-board-ble-pmos.elf: $(OBJS) $(USER_OBJS)
	@	riscv-wch-elf-gcc -march=rv32ec_zmmul_xw -mabi=ilp32e -msmall-data-limit=0 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -T "c:/Users/Laythy/Desktop/CR-LBJ-Receiver-CH32V005-proj/Ld/Link.ld" -nostartfiles -Xlinker --gc-sections -Wl,-Map,"0000----CH32V005F6P6-power-board-ble-pmos.map" --specs=nano.specs --specs=nosys.specs -o "0000----CH32V005F6P6-power-board-ble-pmos.elf" $(OBJS) $(USER_OBJS) $(LIBS) -lprintf
0000----CH32V005F6P6-power-board-ble-pmos.hex: 0000----CH32V005F6P6-power-board-ble-pmos.elf
	@	riscv-wch-elf-objcopy -O ihex "0000----CH32V005F6P6-power-board-ble-pmos.elf" "0000----CH32V005F6P6-power-board-ble-pmos.hex"
0000----CH32V005F6P6-power-board-ble-pmos.lst: 0000----CH32V005F6P6-power-board-ble-pmos.elf
	@	riscv-wch-elf-objdump --all-headers --demangle --disassemble -M xw "0000----CH32V005F6P6-power-board-ble-pmos.elf" > "0000----CH32V005F6P6-power-board-ble-pmos.lst"
0000----CH32V005F6P6-power-board-ble-pmos.siz: 0000----CH32V005F6P6-power-board-ble-pmos.elf
	riscv-wch-elf-size --format=berkeley "0000----CH32V005F6P6-power-board-ble-pmos.elf"

# Other Targets
clean:
	-$(RM) $(OBJS)$(SECONDARY_FLASH)$(SECONDARY_LIST)$(SECONDARY_SIZE)$(S_DEPS)$(S_UPPER_DEPS)$(ASM_DEPS)$(ASM_UPPER_DEPS)$(C_DEPS) 0000----CH32V005F6P6-power-board-ble-pmos.elf

secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE)

.PHONY: all clean dependents

-include ../makefile.targets