# LAB-08 PIN cooldown

320x240 screen. The scrollable guide is on the screen before the controls. The stage band does not cover it.

The guide says: each digit from 1 to 9 shows as * immediately. After the fourth digit the copy is checking... and further keypad input is ignored while that submission is pending. A wrong PIN clears the entry and shows wrong pin for 2 seconds. A timeout is not a wrong PIN. Five wrong PINs for one user start a 60-second cooldown whose copy is Too many attempts. Try again in the remaining seconds. The failure count belongs to the selected user. Switching users starts at 0. Success clears it. Cooldown expiry clears it. BOOT at 0 digits returns to the roster. BOOT at 1 to 3 digits clears them and stays. After the fourth digit BOOT is ignored and is not a second terminal. Sixty seconds after the last digit of a partial entry returns to the roster and discards the digits. Mute during verification cancels locally, turns the display off, and a late success must not undo mute or reveal the PIN. A local cancel does not mean the server rolled back a side effect. Each control names the action the operator is about to take. Scripted mode runs the decided scenarios without taps.

Stages stay wifi_unavailable, connecting, roster, empty_hangout, and muted. PIN entry, checking, wrong pin, and cooldown are a view on the roster stage. There is one session writer, the LAB-07 SessionWriter. This lab does not add a second writer.

Transport uses four attempts, a 1-second attempt timeout, and delays of 1 second, 2 seconds, then 4 seconds. If all four fail, the stage is connecting and the wrong-PIN count is unchanged. Retries reuse the same operation key and the exact request. The server stores one result per key and counts one remote side effect. Evidence stores a key fingerprint, never the PIN and never a hash of the four-digit PIN.

The 1/2/4-second delays, the 2-second wrong-pin copy, the 60-second cooldown, and the 60-second partial-entry return are L1 virtual-clock only. Do not sleep those intervals in tests.

Manual controls, in order: Select user su-01. Enter digit 1. Press BOOT. Submit a wrong PIN. Mute during checking. Unmute. Switch to user su-02. Advance the wrong-pin copy. Advance the cooldown clock by one second. Advance the partial-entry idle clock. The button caption is that action. A digit or a cooldown second updates the existing labels. It does not rebuild the guide.

Scripted mode prints LAB08_READY and then the decided traces. K-ok and K-lost are printed only after the fixture status and body are classified. A missing server, a malformed body, or a transport failure is not successful authentication.

PIN result queue capacity is 2, a provisional lab probe. Overflow of an identified result is one failure overflow and does not start a second login. An unidentified result is not given the current in-flight id. The roster result queue capacity remains 4.

stage_owner=lab07-session

ResourceBudget
  max_live_nodes: 24
  max_transient_bytes: 4096
  max_persistent_bytes: 512
  max_concurrent_operations: 2
  max_stack_or_call_depth: 12
  maximum_input_size: 4

These ResourceBudget numbers are a provisional lab probe. maximum_input_size is 4 PIN digits. The HTTP body cap is 1024 bytes. CONFIG_LV_MEM_SIZE=65536 is the compile default, not a threshold. Do not declare a performance PASS. This lab has a server on port 8768, so S-12 is not applicable.

COM-OPEN-006, COM-OPEN-007, COM-OPEN-008, and COM-OPEN-009 stay OPEN. This lab does not issue a session token, decide a concurrent-key policy, start a retention window, or visually approve the cooldown copy.
