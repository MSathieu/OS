#include <capability.h>
#include <ioports.h>
#include <ipccalls.h>
#include <irq.h>
#include <keyboard.h>
#include <ps2/ps2.h>
#include <ps2/scancode.h>
#include <syslog.h>

static int leds;

static void send_command(int cmd, int arg) {
  while (inb(PS2_PORT_COMMAND) & PS2_STATUS_INPUT)
    ;
  outb(PS2_PORT_DATA, cmd);
  if (arg >= 0) {
    while (inb(PS2_PORT_COMMAND) & PS2_STATUS_INPUT)
      ;
    outb(PS2_PORT_DATA, arg);
  }
  while (1) {
    if (inb(PS2_PORT_COMMAND) & PS2_STATUS_OUTPUT) {
      int response = inb(PS2_PORT_DATA);
      if (response == PS2_RESPONSE_ACK) {
        return;
      }
      if (response == PS2_RESPONSE_RESEND) {
        return send_command(cmd, arg);
      }
    } else {
      wait_irq();
    }
  }
}
static inline void change_leds(int new_leds) {
  if (leds == new_leds || new_leds < 0) {
    return;
  }
  leds = new_leds;
  int arg = 0;
  if (leds & 1 << KBD_LED_CAPS_LOCK) {
    arg |= PS2_LED_CAPS_LOCK;
  }
  if (leds & 1 << KBD_LED_NUM_LOCK) {
    arg |= PS2_LED_NUM_LOCK;
  }
  send_command(PS2_DEVICE_SET_LEDS, arg);
}
int main(void) {
  drop_capability(CAP_NAMESPACE_KERNEL, CAP_KERNEL_PRIORITY);
  outb(PS2_PORT_COMMAND, PS2_CMD_DISABLE_FIRST);
  outb(PS2_PORT_COMMAND, PS2_CMD_DISABLE_SECOND);
  while (inb(PS2_PORT_COMMAND) & PS2_STATUS_OUTPUT) {
    inb(PS2_PORT_DATA);
  }
  outb(PS2_PORT_COMMAND, PS2_CMD_READ_CONFIG);
  uint8_t config = inb(PS2_PORT_DATA);
  config |= PS2_CONFIG_FIRST_INT;
  config &= ~PS2_CONFIG_SECOND_INT;
  config &= ~PS2_CONFIG_TRANSLATION;
  outb(PS2_PORT_COMMAND, PS2_CMD_WRITE_CONFIG);
  outb(PS2_PORT_DATA, config);
  outb(PS2_PORT_COMMAND, PS2_CMD_ENABLE_FIRST);
  send_command(PS2_DEVICE_RESET, -1);
  while (1) {
    if (inb(PS2_PORT_COMMAND) & PS2_STATUS_OUTPUT) {
      int response = inb(PS2_PORT_DATA);
      if (response == PS2_RESPONSE_RESET) {
        break;
      }
      if (response == PS2_RESPONSE_RESET_FAILED) {
        syslog(LOG_ERR, "PS/2 reset failed");
        return 1;
      }
    } else {
      wait_irq();
    }
  }
  bool release = 0;
  bool extended = 0;
  while (1) {
    if (inb(PS2_PORT_COMMAND) & PS2_STATUS_OUTPUT) {
      int scancode = inb(PS2_PORT_DATA);
      if (scancode == SCANCODE_RELEASE) {
        release = 1;
        continue;
      }
      if (scancode == SCANCODE_EXTENDED) {
        extended = 1;
        continue;
      }
      int key;
      if (extended) {
        key = scancode_conversion_extended[scancode];
        extended = 0;
      } else {
        key = scancode_conversion[scancode];
      }
      if (!key) {
        release = 0;
        continue;
      }
      int return_value = send_ipc_call("kbdd", IPC_KBDD_KEYPRESS, key, release, 0, 0, 0);
      release = 0;
      change_leds(return_value);
    } else {
      wait_irq();
    }
  }
}
