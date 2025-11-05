/*
 * ====================================================================
 * [추가] CH9329 번역기 및 KVM 호환성 코드
 * ====================================================================
 */

// KVM 호환성: PIO 포트로 USB 장치 신호를 보내기 위한 TinyUSB Device API 사용
// Note: 이 코드는 Pico의 PIO 포트가 USB Device(출력) 역할을 하고,
//       네이티브 Micro-USB 포트가 USB Host(입력) 역할을 한다고 가정합니다.

// [중요] KVM 호환성을 위해, 이 함수들이 Pico의 Device 포트로 출력됩니다.
extern void tud_hid_keyboard_report (uint8_t report_id, uint8_t modifier, uint8_t keycode[6]);
extern void tud_hid_mouse_report (uint8_t report_id, uint8_t buttons, int8_t x, int8_t y, int8_t wheel);


// [새로운] CH9329 복합 장치 카운터 (복잡한 장치를 구분하는 데 사용)
static uint8_t hid_mouse_count = 0;
static uint8_t hid_kbd_count = 0;

// [새로운] KVM 필터링: 마지막 마우스 리포트를 저장 (상대/절대 좌표 필터링용)
static hid_mouse_report_t last_mouse_report = { 0, 0, 0, 0 };


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
  // [수정] 클럭 설정을 120MHz에서 120MHz로 유지 (TinyUSB 표준)
  set_sys_clock_khz(120000, true); 

  sleep_ms(10);

  multicore_reset_core1();
  // all USB task run in core1
  multicore_launch_core1(core1_main);

  // init device stack on native usb (roothub port0)
  tud_init(0);

  while (true) {
    tud_task(); // tinyusb device task
    tud_cdc_write_flush();
    
    // [추가] CH9329 응답 핸들러 (이전 대화에서 논의했던 ACK 전송 로직)
    // 현재 예제는 CDC 장치만 구현했으므로, ACK 전송은 이 코드가 아니라
    // 'tud_cdc_rx_cb' 내부에 구현되거나, host_app_task 등 외부에서 처리되어야 합니다.
  }

  return 0;
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

  char tempbuf[256];
  int count = sprintf(tempbuf, "[%04x:%04x][%u] HID Interface%u, Protocol = %s\r\n", vid, pid, dev_addr, instance, protocol_str[itf_protocol]);

  tud_cdc_write(tempbuf, count);
  tud_cdc_write_flush();

  // [수정] CH9329의 복합 장치 중 '표준 키보드'와 '상대 마우스'만 선택
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD)
  {
      hid_kbd_count++;
      // [필터링] CH9329는 일반적으로 키보드 인터페이스를 1개만 사용함
      if (hid_kbd_count == 1) {
          tud_cdc_write_str("-> Selected Standard Keyboard Interface\r\n");
          if ( !tuh_hid_receive_report(dev_addr, instance) ) {
              tud_cdc_write_str("Error: cannot request kbd report\r\n");
          }
      }
  } 
  else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)
  {
      hid_mouse_count++;
      // [필터링] CH9329는 마우스 인터페이스를 여러 개 보냄 (상대/절대).
      // 첫 번째 마우스 인터페이스(상대 마우스일 확률 높음)만 선택합니다.
      // 이후의 인터페이스(절대 좌표, 커스텀)는 무시합니다.
      if (hid_mouse_count == 1) {
          tud_cdc_write_str("-> Selected Standard Mouse Interface\r\n");
          if ( !tuh_hid_receive_report(dev_addr, instance) ) {
              tud_cdc_write_str("Error: cannot request mouse report\r\n");
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
  
  char tempbuf[256];
  int count = sprintf(tempbuf, "[%u] HID Interface%u is unmounted\r\n", dev_addr, instance);
  tud_cdc_write(tempbuf, count);
  tud_cdc_write_flush();
}

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report, uint8_t keycode)
{
  // ... (기존 코드 유지)
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
  static hid_keyboard_report_t prev_report = { 0, 0, {0} }; // previous report to check key released
  
  // [수정] CDC 출력 대신 KVM (PIO Port)로 재전송
  tud_hid_keyboard_report(0, report->modifier, report->keycode);
  
  // [수정] 키 눌림 상태 업데이트 (필터링 로직용)
  prev_report = *report;

  // [수정] KVM으로 보고서를 보냈으므로, 여기서는 CDC 플러시는 필요 없음
  // if (flush) tud_cdc_write_flush(); 
}


// send mouse report to usb device CDC
static void process_mouse_report(uint8_t dev_addr, hid_mouse_report_t const * report)
{
  // [수정] 절대 좌표 필터링: 마우스 리포트가 0이 아닐 때만 전송
  // CH9329가 절대 좌표를 보낼 때 (X/Y가 큰 값일 경우)는 무시
  if (report->x == 0 && report->y == 0 && report->wheel == 0 && report->buttons == last_mouse_report.buttons) {
      // 움직임이나 버튼 변화가 없으면 전송하지 않음
      return;
  }

  // [수정] KVM (PIO Port)로 재전송
  tud_hid_mouse_report(0, report->buttons, report->x, report->y, report->wheel);
  
  // [수정] 상태 저장
  last_mouse_report = *report;

  // [기존 CDC 디버그 코드] (디버깅을 원하면 활성화)
  /*
  char tempbuf[32];
  int count = sprintf(tempbuf, "[%u] %c%c%c %d %d %d\r\n", dev_addr, l, m, r, report->x, report->y, report->wheel);
  tud_cdc_write(tempbuf, count);
  tud_cdc_write_flush();
  */
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
          process_mouse_report(dev_addr, (hid_mouse_report_t const*) report );
      }
    break;

    default: 
      // [필터링] 나머지 모든 장치(절대 좌표, 커스텀 HID 등)는 무시하고 버립니다.
    break;
  }

  // continue to request to receive report
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    tud_cdc_write_str("Error: cannot request report\r\n");
  }
}
