"""LAB-03 trace/v1 reductions. Missing target observations never become zero/pass."""
import collections
import json
import math

REQUIRED = ['callback_us','raster_us','decode_us','intent_play','worker_start','first_codec_write',
            'first_frame_ui','pause_request','pause_worker_effect','pause_ui_effect','resume_request',
            'resume_worker_effect','stop_ui_effect','ui_reconcile','renderer_backlog','worker_stack',
            'ui_stack','internal_heap','psram_heap','lvgl_heap','allocation','resident_bytes',
            'reveal_lateness_ms','harness_done','page_applied','rapid_switch','sustained_cycle']
EXTERNAL = ['first_audible_sample','synchronized_screen_audio','audio_underrun_count_duration',
            'dac_position','lcd_visible_frame_timestamp','renderer_cpu_utilization','cache_drift_assessment']

def distribution(values):
    if not values: return {'n':0,'p50':None,'p95':None,'p99':None,'worst':None}
    values=sorted(values)
    return {'n':len(values),**{f'p{p}':values[max(0,math.ceil(p/100*len(values))-1)] for p in (50,95,99)},'worst':values[-1]}

def parse(text,run_id):
    rows=[]; malformed=0
    lines=text.splitlines()
    ready=[i for i,line in enumerate(lines) if line.startswith('LAB03_READY ')]
    excluded=sum(line.startswith('LAB03_TRACE ') for line in lines[:ready[0]]) if ready else 0
    scope=lines[ready[0]+1:] if ready else lines
    observed_build=None
    if ready:
        fields=dict(part.split('=',1) for part in lines[ready[0]].split()[1:] if '=' in part)
        observed_build=fields.get('build')
    for line in scope:
        if not line.startswith('LAB03_TRACE '): continue
        try:
            row=json.loads(line[len('LAB03_TRACE '):])
            for key in ('event','t_us','operation_id','session_id','message_id','position_ms','rendered','value','aux'):
                if key not in row: raise ValueError(key)
            rows.append(row)
        except (ValueError,TypeError): malformed+=1
    events=collections.defaultdict(list)
    ops=collections.defaultdict(list)
    for row in rows:
        events[row['event']].append(row)
        if row['operation_id']: ops[row['operation_id']].append(row)
    # Single UI invalidation presents this stroke range. Expand off the UI owner.
    prior_batch={}
    for batch in events['reveal_batch']:
        previous=prior_batch.get(batch['operation_id'],next((r['t_us'] for r in events['intent_play'] if r['operation_id']==batch['operation_id']),batch['t_us']))
        returned=any(previous<r['t_us']<batch['t_us'] and r['message_id']!=batch['message_id'] for r in events['focus'])
        prior_batch[batch['operation_id']]=batch['t_us']
        if 0 < batch['aux'] <= 4096 and 0 <= batch['rendered'] <= batch['value'] <= batch['aux']:
            for index in range(batch['rendered'],batch['value']):
                due=index*2800//batch['aux']
                sample={**batch,'event':'reveal_lateness_ms','rendered':index,
                    'value':max(0,batch['position_ms']-due),'aux':due}
                events['reveal_lateness_ms'].append(sample)
                events['focus_return_reveal_ms' if returned else 'continuous_focus_reveal_ms'].append(sample)
    missing=[e for e in REQUIRED if not events[e]]+EXTERNAL
    if not ready: missing.append('fresh_ready_marker')
    if len(ready)>1: missing.append('unexpected_device_reboot')
    losses=[r['value'] for r in events['trace_loss']+events['harness_done']]
    drops=max(losses) if losses else None
    loss='OBSERVED' if drops or malformed else 'NONE' if drops==0 else 'UNKNOWN'
    control_latencies={name:[] for name in ('pause_request_to_worker_us','pause_request_to_ui_us','resume_request_to_worker_us','resume_request_to_ui_us')}
    metrics={name:distribution([r['value'] for r in events[name]])
             for name in ('callback_us','raster_us','decode_us','reveal_lateness_ms','continuous_focus_reveal_ms','focus_return_reveal_ms','worker_busy_us')}
    network_intervals=[]; start_frame=[]; start_codec=[]; cancel_ui=[]; cancel_terminal=[]; terminals={}; position=[]; pauses=[]; inactive_stops=[]
    accepted={r['operation_id'] for r in events['intent_play']}
    for op,items in ops.items():
        items.sort(key=lambda r:r['t_us'])
        starts=[r for r in items if r['event']=='intent_play']
        writes=[r for r in items if r['event']=='first_codec_write']
        ends=[r for r in items if r['event'].startswith('terminal_')]
        terminals[str(op)]={'accepted':op in accepted,'worker_starts':sum(r['event']=='worker_start' for r in items),
                             'count':len(ends),'outcomes':[r['event'] for r in ends]}
        if starts and writes: start_codec.append(writes[0]['t_us']-starts[0]['t_us'])
        frames=[r for r in items if r['event']=='first_frame_ui']
        if starts and frames: start_frame.append(frames[0]['t_us']-starts[0]['t_us'])
        buffering=[r for r in items if r['event']=='network_buffering_start']
        buffered=[r for r in items if r['event']=='network_buffering_end']
        if buffering:
            end=buffered[0] if buffered else ends[0] if ends else None
            network_intervals.append({'operation_id':op,'message_id':buffering[0]['message_id'],
                'start_us':buffering[0]['t_us'],'end_us':end['t_us'] if end else None,
                'duration_us':end['t_us']-buffering[0]['t_us'] if end else None,
                'outcome':'ready' if buffered else end['event'] if end else 'open'})
        requests=[r for r in items if r['event'] in ('stop_request','cancel_audio_request','cancel_sketch_request','signout_request','switch_request')]
        effects=[r for r in items if r['event'] in ('stop_ui_effect','switch_ui_effect')]
        for request in requests:
            after=[r for r in effects if r['t_us']>=request['t_us']]
            if after: cancel_ui.append(after[0]['t_us']-request['t_us'])
            after=[r for r in ends if r['t_us']>=request['t_us']]
            before=[r for r in ends if r['t_us']<request['t_us']]
            if after and not before: cancel_terminal.append(after[0]['t_us']-request['t_us'])
            else: inactive_stops.append({'operation_id':op,'request_t_us':request['t_us']})
        for prefix in ('pause','resume'):
            for request in [r for r in items if r['event']==prefix+'_request']:
                for effect in ('worker','ui'):
                    effects_after=[r for r in items if r['event']==prefix+'_'+effect+'_effect' and r['t_us']>=request['t_us']]
                    if effects_after: control_latencies[prefix+'_request_to_'+effect+'_us'].append(effects_after[0]['t_us']-request['t_us'])
        samples=[{'t_us':r['t_us'],'submitted_pcm_ms':r['position_ms'],'rendered':r['rendered'],
                  'stage':r['value']} for r in items if r['event']=='ui_reconcile']
        position.append({'operation_id':op,'updates':samples})
        pauses += [{'operation_id':op,**r} for r in items if r['event'] in ('pause_worker_effect','pause_ui_effect','resume_worker_effect')]
    terminal_ok=bool(accepted) and all(terminals[str(op)]['count']==1 for op in accepted)
    if not terminal_ok: missing.append('exactly_one_terminal_per_accepted_operation')
    resource={}
    for name in ('internal_heap','psram_heap','lvgl_heap','heap_min_free','worker_stack','ui_stack','resident_bytes','allocation','heap_total','image_counts'):
        samples=events[name]
        resource[name]={'samples':len(samples),'first':samples[0] if samples else None,
                        'last':samples[-1] if samples else None,
                        'value_min':min((r['value'] for r in samples),default=None),
                        'value_max':max((r['value'] for r in samples),default=None),
                        'aux_min':min((r['aux'] for r in samples),default=None),
                        'aux_max':max((r['aux'] for r in samples),default=None)}
    rapid=len(events['rapid_switch']); sustained=events['sustained_cycle']
    span=(sustained[-1]['t_us']-sustained[0]['t_us'])/1e6 if sustained else None
    if rapid<60: missing.append('20_A_B_A_cycles')
    if span is None or span<590: missing.append('sustained_600s')
    metrics.update({name:distribution(values) for name,values in control_latencies.items()})
    # Stage-specific evidence must come from operation traces, not just elapsed harness time.
    def ordered(items, names):
        cursor=-1
        for name in names:
            found=next((r['t_us'] for r in items if r['event']==name and r['t_us']>cursor),None)
            if found is None: return False
            cursor=found
        return True
    audio_cancel=sketch_cancel=False
    failure=False; audio_only=False; pause_resume=False; dense=False; page_during=False; signout=False
    for op,items in ops.items():
        items=sorted(items,key=lambda r:r['t_us'])
        if ordered(items,['audio_fetch_start','cancel_audio_request','audio_fetch_end','terminal_cancelled']): audio_cancel=True
        if ordered(items,['sketch_fetch_start','cancel_sketch_request','sketch_fetch_end','terminal_cancelled']): sketch_cancel=True
        if any(r['message_id']=='D' and r['event']=='terminal_failed' for r in items): failure=True
        if any(r['message_id']=='C' and r['event']=='first_codec_write' for r in items): audio_only=True
        if any(r['message_id']=='B' and r['event']=='media_ready' and r['value']==4096 for r in items): dense=True
        if ordered(items,['intent_play','pause_request','pause_worker_effect','resume_request','resume_worker_effect']): pause_resume=True
        if ordered(items,['signout_request','terminal_cancelled']): signout=True
        writes=[r['t_us'] for r in items if r['event']=='first_codec_write']
        ends=[r['t_us'] for r in items if r['event'].startswith('terminal_')]
        if writes and ends and any(writes[0]<=r['t_us']<=ends[-1] for r in events['page_applied']): page_during=True
    focus_b_play=False
    for items in ops.values():
        starts=[r for r in items if r['event']=='intent_play' and r['message_id']=='B']
        writes=[r for r in items if r['event']=='first_codec_write' and r['message_id']=='B']
        if starts and writes and writes[0]['t_us']>=starts[0]['t_us']:
            previous=[r for r in events['focus'] if r['t_us']<=starts[0]['t_us']]
            if previous and max(previous,key=lambda r:r['t_us'])['message_id']=='B': focus_b_play=True
    rapid_rows=sorted(events['rapid_switch'],key=lambda r:r['t_us'])
    rapid_cycles=0
    for index in range(0,len(rapid_rows)-2,3):
        group=rapid_rows[index:index+3]
        if ([r['message_id'] for r in group]==list('ABA') and
            all(r['operation_id'] in accepted and terminals[str(r['operation_id'])]['count']==1 for r in group)):
            rapid_cycles+=1
    dense_completed=any(r['message_id']=='B' and r['event']=='terminal_completed' and r['rendered']==4096 for r in rows)
    offscreen=False
    for op,items in ops.items():
        pause_times=[r['t_us'] for r in items if r['event']=='pause_worker_effect' and r['message_id']=='B']
        resume_times=[r['t_us'] for r in items if r['event']=='resume_worker_effect' and r['message_id']=='B']
        if pause_times and resume_times:
            offscreen |= any(pause_times[-1]<r['t_us']<resume_times[-1] and r['message_id']!='B' for r in events['focus'])
    scenarios={'play_pause_resume':pause_resume,'focus_B_play_B':focus_b_play,
        'rapid_20_A_B_A':rapid_cycles>=20,'cancel_during_audio_fetch':audio_cancel,
        'cancel_during_sketch_fetch':sketch_cancel,'new_page_during_playback':page_during,
        'signout_during_operation':signout,'asset_failure':failure,'audio_only':audio_only,
        'maximum_dense_sketch':dense_completed,'pause_then_move_offscreen':offscreen,'sustained_600s':span is not None and span>=590}
    missing += ['scenario:'+name for name,observed in scenarios.items() if not observed]
    internal_total=resource['heap_total']['value_max']; psram_total=resource['heap_total']['aux_max']
    heap_floor=resource['heap_min_free']
    high_water={'internal_regional_minimum_used_upper_bound_bytes':internal_total-heap_floor['value_min'] if internal_total is not None and heap_floor['value_min'] is not None else None,
                'psram_regional_minimum_used_upper_bound_bytes':psram_total-heap_floor['aux_min'] if psram_total is not None and heap_floor['aux_min'] is not None else None,
                'lvgl_used_bytes':resource['lvgl_heap']['value_max'],
                'internal_sampled_peak_used_bytes':internal_total-resource['internal_heap']['value_min'] if internal_total is not None and resource['internal_heap']['value_min'] is not None else None,
                'psram_sampled_peak_used_bytes':psram_total-resource['psram_heap']['value_min'] if psram_total is not None and resource['psram_heap']['value_min'] is not None else None}
    reserve={'internal_next_plus_reserve_bytes':8192+16384,'psram_next_plus_reserve_bytes':96000+32768,
             'internal_largest_min':resource['internal_heap']['aux_min'],'psram_largest_min':resource['psram_heap']['aux_min']}
    metrics.update({'observed_build_id':observed_build,'ready_marker_count':len(ready),'pre_ready_trace_rows_excluded':excluded,'scenarios_observed':scenarios,'accepted_rapid_ABA_cycles':rapid_cycles,'heap_used_high_water':high_water,'declared_reserve_check':reserve})
    metrics.update({'start_to_codec_write_us':distribution(start_codec),
        'start_to_first_ui_frame_us':distribution(start_frame),'start_to_audible_us':distribution([]),'cancel_to_ui_us':distribution(cancel_ui),
        'cancel_to_worker_terminal_us':distribution(cancel_terminal),
        'inactive_or_unpaired_stops':inactive_stops,'terminal_events':terminals,'exactly_one_terminal':terminal_ok if accepted else None,
        'stale_results_rejected':len(events['stale_rejected']) if rows else None,
        'allocation_failures':max((r['value'] for r in events['runtime_allocation_failures']),default=None),
        'psram_wrapper_allocation_attempts':max((r['value'] for r in events['allocation']),default=None),
        'audio_underruns':None,'network_buffering_intervals':network_intervals,
        'temporary_network_read_waits':len(events['network_read_wait']) if rows else None,
        'renderer_backlog_max':max((r['value'] for r in events['renderer_backlog']),default=None),
        'segments_per_update_max':max((r['aux'] for r in events['raster_us']),default=None),
        'resources':resource,'position_and_progress':position,'pause_observations':pauses,
        'rapid_switches':rapid,'sustained_span_seconds':span,
        'criteria_status':'PROVISIONAL_UNMEASURED','experiment_verdict':'INCOMPLETE',
        'cache_bytes':0 if events['resident_bytes'] else None})
    return {'document':'family-link.lab-metrics/v2','run_id':run_id,'schema_version':'lab03-trace/v1',
            'parser_version':'lab03-parser/v1','evidence':{'status':'INCOMPLETE','trace_loss':loss,
            'firmware_trace_loss':{'status':loss,'dropped_events':drops,'malformed_rows':malformed},
            'missing':missing},'lab_metrics':metrics}
