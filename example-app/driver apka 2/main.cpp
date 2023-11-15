#include <iostream>
#include <Windows.h>
#include <subauth.h>

#define IOCTL_SIOCTL_METHOD_BUFFERED CTL_CODE(40000, 0x902, METHOD_BUFFERED, FILE_ANY_ACCESS)

enum {
   input_type_initialize,
   input_type_read
};

struct input_data {
   int type;
   union {
      struct {
         int process_id;
      }initialize;
      struct {
         UINT64 address;
      }read;
   };
};

namespace driver {
   HANDLE device;
   uintptr_t peb_address;
   bool init() {
      device = CreateFileA("\\\\.\\dupa", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      return device != INVALID_HANDLE_VALUE;
   }
   bool attach(DWORD process_id) {
      input_data input;
      input.type = input_type_initialize;
      input.initialize.process_id = process_id;
      return DeviceIoControl(device, IOCTL_SIOCTL_METHOD_BUFFERED, &input, sizeof(input), &peb_address, sizeof(peb_address), nullptr, nullptr);
   }
   bool read(uintptr_t address, void* buf, int size) {
      input_data input;
      input.type = input_type_read;
      input.read.address = address;
      return DeviceIoControl(device, IOCTL_SIOCTL_METHOD_BUFFERED, &input, sizeof(input), buf, size, nullptr, nullptr);
   }
   template <typename t> t read(uintptr_t address) {
      t buf;
      if (!read(address, &buf, sizeof(t))) return t{};
      return buf;
   }
}

uintptr_t find_module(std::string_view module_name) {
   uintptr_t peb_ldr = driver::read<uintptr_t>(driver::peb_address + 0x18);
   if (!peb_ldr) {
      std::cout << "[!] no peb ldr\n";
      return 0;
   }
   uintptr_t flink = driver::read<uintptr_t>(peb_ldr + 0x10);
   uintptr_t it = flink;
   do {
      UNICODE_STRING name = driver::read<UNICODE_STRING>(it + 0x58);
      if (name.Buffer && name.Length / 2 == module_name.size()) {
         std::unique_ptr<wchar_t[]> name_buffer = std::make_unique<wchar_t[]>(name.Length / 2);
         if (driver::read((uintptr_t)name.Buffer, name_buffer.get(), name.Length)) {
            bool ok = true;
            for (size_t i = 0; i < module_name.size(); i++) {
               if (towlower(name_buffer[i]) != tolower(module_name[i])) {
                  ok = false;
                  break;
               }
            }
            if (ok) return driver::read<uintptr_t>(it + 0x30);
         }
      }
      it = driver::read<uintptr_t>(it);
   } while (it != flink);
   return 0;
}

int main() {

   if (!driver::init()) {
      std::cout << "[!] no driver\n";
      return 1;
   }

   HWND window = FindWindowA("SDL_app", "Counter-Strike 2");
   if (!window) {
      std::cout << "[!] window not found\n";
      return 1;
   }

   DWORD process_id;
   GetWindowThreadProcessId(window, &process_id);

   if (!driver::attach(process_id)) {
      std::cout << "[!] failed to attach\n";
      return 1;
   }

   uintptr_t client = find_module("client.dll");
   if (!client) {
      std::cout << "[!] client not found (no game?)\n";
      return 1;
   }
   std::cout << std::hex << client << "\n";
}