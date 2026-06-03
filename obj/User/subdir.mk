################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/BLE.c \
../User/GPS.c \
../User/Tick.c \
../User/ch32v00X_it.c \
../User/main.c \
../User/system_ch32v00X.c 

C_DEPS += \
./User/BLE.d \
./User/GPS.d \
./User/Tick.d \
./User/ch32v00X_it.d \
./User/main.d \
./User/system_ch32v00X.d 

OBJS += \
./User/BLE.o \
./User/GPS.o \
./User/Tick.o \
./User/ch32v00X_it.o \
./User/main.o \
./User/system_ch32v00X.o 



# Each subdirectory must supply rules for building sources it contributes
User/%.o: ../User/%.c
	@	riscv-wch-elf-gcc -march=rv32ec_zmmul_xw -mabi=ilp32e -msmall-data-limit=0 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"c:/Users/Laythy/Desktop/CR-LBJ-Receiver-CH32V005-proj/Debug" -I"c:/Users/Laythy/Desktop/CR-LBJ-Receiver-CH32V005-proj/Core" -I"c:/Users/Laythy/Desktop/CR-LBJ-Receiver-CH32V005-proj/User" -I"c:/Users/Laythy/Desktop/CR-LBJ-Receiver-CH32V005-proj/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
