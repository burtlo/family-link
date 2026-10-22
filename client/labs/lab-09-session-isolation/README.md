# LAB-09 signed-in probe

320x240 screen. The scrollable guide is on the screen before the controls. The stage band ends where the guide begins.

The guide says: This is a signed-in probe, not a product inbox. Four operations are fetch the inbox, fetch one image, write playback state, and update the profile. Each one keeps the operation id and session generation from the moment you submit it. The screen shows whose session is active and whether a result was applied. Mute starts each operation for user A. After mute, a late result stays stale or cancelled, user A's bytes stay hidden, the display is off, and the late result does not restore the stage. Sign out before the result arrives and the result is rejected. The stage stays signed out. Sign in as user B before user A's result arrives. A's result is not applied to B's cache, and B's session generation is different from A's. If the fixture session expires before the response, nothing is applied, the stage is not restored, and there is one terminal that is not success. Duplicate and out-of-order inbox hints do not create duplicate card ids. Each button names the action you are about to take. Scripted mode runs the decided scenarios without taps.

There is one session writer, the LAB-07 SessionWriter. This lab does not add a second writer. Fixture transport and the named controls stay outside that owner.

Manual controls, in order: Sign in as user A. Fetch inbox. Fetch media. Write playback state. Update profile. Mute. Sign out. Sign in as user B. The button caption is that action. A result updates the existing session and applied labels. It does not rebuild the guide.

Scripted mode prints LAB09_READY and then runs I-mute, I-signout, I-switch, I-push, and I-expire-client. Those traces are printed only after the fixture status and body are classified. A missing server, a malformed body, or a transport failure is not a signed-in success and is not an applied result.

Decided experiment inputs are I-mute, I-signout, I-switch, I-push, and I-expire-client. I-confused, I-revoke-branches, and I-expire-http stay exploratory and out of that list. I-confused records whether user B's fixture counter incremented. I-revoke-branches records fixture modes revoke-on-mute and leave-server-session. I-expire-http records the HTTP status integer. None of those approve issuance, revocation, or an error-code mapping.

The inbound result queue capacity is 8, a provisional lab probe. Overflow of an identified result is one failure overflow and does not drop a terminal silently. An unidentified result is not given the current in-flight id. Cleanup clears the one image buffer (image_bytes 0) and stops port 8769.

stage_owner=lab07-session

ResourceBudget
  max_live_nodes: 24
  max_transient_bytes: 28800
  max_persistent_bytes: 512
  max_concurrent_operations: 1
  max_stack_or_call_depth: 12
  maximum_input_size: 28800

These ResourceBudget numbers are a provisional lab probe. The image buffer is 28800 bytes. The inbox page holds 16 summaries. CONFIG_LV_MEM_SIZE=65536 is the compile default, not a threshold. Do not declare a performance PASS. This lab has a server on port 8769, so S-12 is not applicable. Mute-to-cancel time stays UNMEASURED until a device trace records it.

COM-OPEN-001: OPEN. COM-OPEN-003: OPEN. Both refer to docs/product/communication-contracts/user-scoped-envelope.md. This lab does not approve a token format, a revocation protocol, inbox paging, or profile field meaning.

Logical version: LAB-09/v1 = firmware client/labs/lab-09-session-isolation + server lab09-server/v1 + fixture lab09-fixture/v1.
