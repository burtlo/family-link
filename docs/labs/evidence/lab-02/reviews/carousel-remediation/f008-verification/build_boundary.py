from pathlib import Path
import re,shlex,subprocess,json
out=Path('/tmp/lab02-f008-verify'); root=Path.cwd(); lab=root/'client/labs/lab-02-bounded-inbox'; core=lab/'components/lab02'
cache=Path(json.loads((out/'normal/runtime.json').read_text())['cache'])
startup=(lab/'tests/test_http_startup_native.c').read_text().replace('bool lab02_board_wifi_connect(void) { return true; }','static bool wifi_ok;\nbool lab02_board_wifi_connect(void) { return wifi_ok; }')
(out/'startup-adapter.c').write_text(startup)
base=(lab/'tests/c0_runtime/test_carousel_baseline.c').as_posix()
test='''#define main baseline_main
#include "%s"
#undef main
#define main startup_main
#include "startup-adapter.c"
#undef main
static bool hook_enabled = true;
static unsigned held_hook_calls;
void verifier_before_boot_release(void) {
 if (!hook_enabled) return;
 held_hook_calls++;
 owner_pumping=true;
 for (int i=0;i<6;i++) lab02_carousel_pump(&carousel);
 assert(lab02_session_snapshot().load_stage==LAB02_LOAD_ERROR);
 lv_obj_send_event(carousel.count_lab, LV_EVENT_CLICKED, NULL);
 lv_obj_send_event(carousel.count_lab, LV_EVENT_CLICKED, NULL);
 for (int i=0;i<3;i++) lab02_carousel_pump(&carousel);
 owner_pumping=false;
 assert(lab02_session_snapshot().load_stage==LAB02_LOAD_SHELL);
 assert(lab02_session_metrics().fetch_retry==1);
 lab02_fetch_req_t req; assert(lab02_session_http_initial_request(&req));
 assert(req.attempt==2);
 unsigned before=task_calls;
 for(int i=0;i<10;i++) lab02_page_provider_http_service_initial_retry();
 assert(task_calls==before);
 lab02_fetch_req_t after; assert(lab02_session_http_initial_request(&after));
 assert(after.op_id==req.op_id && after.session_id==req.session_id && after.attempt==2);
 puts("INDEPENDENT_BOUNDARY old_boot_posted_and_running restart_retained PASS");
}
int main(int argc,char **argv) {
 assert(argc==2); scenario="held-boot-exit"; artifact_dir=argv[1];
 lv_init(); lv_display_t *d=lv_display_create(320,240);
 lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB888);
 lv_display_set_buffers(d,drawbuffer,NULL,sizeof(drawbuffer),LV_DISPLAY_RENDER_MODE_FULL);
 lv_display_set_flush_cb(d,flush);
 assert(lab02_carousel_init(&carousel));
 lab02_page_provider_http_start_io(); assert(task_calls==1);
 assert(boot_entry); boot_entry(boot_arg);
 assert(held_hook_calls==1 && deleted==1 && task_calls==1);
 hook_enabled=false; wifi_ok=true;
 lab02_host_hold_admission(true);
 lab02_page_provider_http_service_initial_retry(); assert(task_calls==1);
 lab02_host_hold_admission(false);
 lab02_page_provider_http_service_initial_retry(); assert(task_calls==2);
 lab02_page_provider_http_service_initial_retry(); assert(task_calls==2);
 boot_entry(boot_arg);
 owner_pumping=true;
 for(int i=0;i<8;i++) lab02_carousel_pump(&carousel);
 owner_pumping=false;
 assert(task_calls==3 && http_calls==1 && deleted==2);
 assert(lab02_session_snapshot().load_stage==LAB02_LOAD_READY);
 assert(lab02_session_metrics().fetch_retry==1);
 puts("INDEPENDENT_BOUNDARY held_boot_exit_then_busy_command_then_once_ready PASS");
 return 0;
}
'''%base
(out/'boundary.c').write_text(test)
provider=(core/'page_provider_http.c').read_text()
provider=provider.replace('static atomic_bool s_boot_running;', 'extern void verifier_before_boot_release(void);\nstatic atomic_bool s_boot_running;')
provider=provider.replace('atomic_store(&s_boot_running, false);','verifier_before_boot_release();\n        atomic_store(&s_boot_running, false);')
(out/'provider-instrumented.c').write_text(provider)
flags=(cache/'CMakeFiles/lab02_initial_retry.dir/flags.make').read_text()
opts=[]
for k in ['C_DEFINES','C_INCLUDES','C_FLAGS']:
 opts+=shlex.split(re.search(r'^'+k+r' = (.*)$',flags,re.M).group(1))
commands=[]
for source in ['boundary','provider-instrumented']:
 cmd=['cc','-std=c11',*opts,'-c',str(out/(source+'.c')),'-o',str(out/(source+'.o'))]
 commands.append(cmd); subprocess.run(cmd,check=True)
link=shlex.split((cache/'CMakeFiles/lab02_initial_retry.dir/link.txt').read_text())
link=[str(out/'boundary.o') if s.endswith('/test_initial_retry_runtime.c.o') else str(out/'provider-instrumented.o') if s.endswith('/page_provider_http.c.o') else s for s in link]
link[link.index('-o')+1]=str(out/'boundary')
commands.append(link); subprocess.run(link,cwd=cache,check=True)
commands.append([str(out/'boundary'),str(out)])
(out/'boundary-commands.json').write_text(json.dumps(commands,indent=2))
p=subprocess.run(commands[-1],capture_output=True,text=True,timeout=10)
(out/'boundary.log').write_text(p.stdout+p.stderr); assert p.returncode==0,p
