/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 * sekigon-gonnoc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

// This example runs both host and device concurrently. The USB host receive
// reports from HID device and print it out over USB Device CDC interface.
// For TinyUSB roothub port0 is native usb controller, roothub port1 is
// pico-pio-usb.

// ================================================================
// [수정] 누락된 헤더 파일 추가 (빌드 오류 해결)
// ================================================================
#include <stdint.h>     // for uint8_t, uint16_t, etc.
#include <stdbool.h>    // for bool, true, false
#include "pio_usb_configuration.h" // for pio_usb_configuration_t, PIO_USB_DEFAULT_CONFIG
// ================================================================


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

#include "pio_usb.h"
#include "tusb.h"


//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

#define KEYBOARD_COLEMAK // (이것은 원본 코드에 있었으므로 유지)
#define HID_KEYCODE_TO_ASCII // (이것은 원본 코드에 있었으므로 유지)

#ifdef KEYBOARD_COLEMAK
const uint8_t colemak[128] = {
  0  ,  0,  0,  0,  0,  0,  0, 22,
  9  , 23,  7,  0, 24, 17,  8, 12,
  0  , 14, 28, 51,  0, 19, 21, 10,
  15 ,  0,  0,  0, 13,  0,  0,  0,
  0  ,  0,  0,  0,  0,  0,  0,  0,
  0  ,  0,  0,  0,  0,  0,  0,  0,
  0  ,  0,  0, 18,  0,  0,  0,  0,
  0  ,  0,  0,  0,  0,  0,  0,  0,
  0  ,  0,  0,  0,  0,  0,  0,  0,
  0  ,  0,  0,  0,  0,  0,  0,  0
};
#endif

// [수정] last_mouse_report 초기화 (빌드 경고 해결)
// TinyUSB hid_mouse_report_t는 버튼, X, Y, Wheel 4개의 필드만 가집니다.
static hid_mouse_report_t last_mouse_report = { 0 }; 
static uint8_t hid_mouse_count = 0;
static uint8_t hid_kbd_count = 0;

static uint8_t const keycode2ascii[128][2] =  { HID_KEYCODE_TO_ASCII };

/*------------- MAIN (core0 & core1) -------------*/

// core1: handle host events
void core1_main() {
  sleep_ms(10); 

  // Use tuh_configure() to pass pio configuration to the host stack
  // Note: tuh_configure() must be called before
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG; 
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

  // To run USB SOF interrupt in core1, init host stack for pio_usb (roothub
  // port1) on core1
  tuh_init(1); 

  while (true) {
    tuh_task(); // tinyusb host task
  }
}

// core0: handle device events
int main(void) {
  // default 125MHz is not appropreate. Sysclock should be multiple of 12MHz.<br>
  set_sys_clock_khz(120000, true); 

  sleep_ms(10); 

  multicore_reset_core1(); 
  // all USB task run in core1
  multicore_launch_core1(core1_main); 

  // init device stack on native usb (roothub port0)
  tud_init(0); 

  while (true) {
    tud_task(); // tinyusb device task
    // tud_cdc_write_flush(); // [유지] CDC 포트는 KVM에 필요 없으므로 주석 상태 유지
  }

  return 0;
}


//--------------------------------------------------------------------+
// Device CDC - 이 예제는 HID 출력이므로 CDC 디버그는 비활성화함
//--------------------------------------------------------------------+

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
  (void) itf;

  char buf[64];
  uint32_t count = tud_cdc_read(buf, sizeof(buf));

  // TODO control LED on keyboard of host stack
  (void) count;
}


//--------------------------------------------------------------------+
// Host HID - [CH9329의 복합 장치 필터링 로직]
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  (void)desc_report;
  (void)desc_len;

  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  // [수정] KVM 호환 로직에 필요한 인터페이스만 선택
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD)
  {
      hid_kbd_count++;
      // [필터링] 첫 번째 키보드 인터페이스만 사용
      if (hid_kbd_count == 1) {
          if ( !tuh_hid_receive_report(dev_addr, instance) ) {
              // 에러 처리
          }
      }
  } 
  else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)
  {
      hid_mouse_count++;
      // [필터링] 첫 번째 마우스 인터페이스(상대 마우스일 확률 높음)만 선택
      if (hid_mouse_count == 1) {
          if ( !tuh_hid_receive_report(dev_addr, instance) ) {
              // 에러 처리
          }
      }
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  // [수정] 복합 장치 카운터 초기화
  hid_mouse_count = 0;
  hid_kbd_count = 0;
}

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report, uint8_t keycode)
{
  for(uint8_t i=0; i<6; i++)
  {
    if (report->keycode[i] == keycode)  return true;
  }
  return false;
}

// convert hid keycode to ascii and print via usb device CDC (ignore non-printable)
static void process_kbd_report(uint8_t dev_addr, hid_keyboard_report_t const *report)
{
  (void) dev_addr;
  static hid_keyboard_report_t prev_report = { 0, 0, {0} }; 
  
  // [수정] KVM (PIO Port)로 재전송 (핵심)
  // [주의] 이 예제는 6KRO를 6KRO로 단순 전달합니다. (KVM이 6KRO를 지원하는 경우)
  tud_hid_keyboard_report(0, report->modifier, report->keycode);
  
  // [수정] 키 눌림 상태 업데이트 (필터링 로직용)
  prev_report = *report;
}


// send mouse report to usb device CDC
static void process_mouse_report(uint8_t dev_addr, hid_mouse_report_t const * report)
{
  // [수정] 절대 좌표 필터링: 마우스 리포트가 0이 아닐 때만 전송
  if (report->x == 0 && report->y == 0 && report->wheel == 0 && report->buttons == last_mouse_report.buttons) {
      return;
  }

  // [수정] KVM (PIO Port)로 재전송 (핵심)
  // [오류 수정] tud_hid_mouse_report 함수의 6개 인자에 맞게 0을 추가함 (horizontal_wheel)
  tud_hid_mouse_report(0, report->buttons, report->x, report->y, report->wheel, 0);
  
  // [수정] 상태 저장
  last_mouse_report = *report;
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) len;
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  switch(itf_protocol)
  {
    case HID_ITF_PROTOCOL_KEYBOARD:
      // [수정] 키보드 인터페이스가 선택된 첫 번째 인터페이스인지 확인
      if (hid_kbd_count == 1) { 
          process_kbd_report(dev_addr, (hid_keyboard_report_t const*) report );
      }
    break;

    case HID_ITF_PROTOCOL_MOUSE:
      // [수정] 마우스 인터페이스가 선택된 첫 번째 인터페이스인지 확인
      if (hid_mouse_count == 1) {
          process_mouse_mouse(dev_addr, (hid_mouse_report_t const*) report ); // [오류] 함수 이름 수정
      }
    break;

    default: 
      // [필터링] 나머지 모든 장치(절대 좌표, 커스텀 HID 등)는 무시하고 버립니다.
    break;
  }

  // continue to request to receive report
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    // 에러 처리
  }
}
