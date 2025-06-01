# fwdbg_daemon_bp.py
import lldb
import os
import sys

# --- Metrics (unchanged) ---
daemon_metrics = {
    'pop_attempts': 0,
    'pops_successful': 0,
    'pops_failed_empty': 0,
    'pops_failed_seq_mismatch': 0,
    'getbytes_calls': 0,
    'fillpacketdata_calls': 0,
    'cleanup_dispatchers_calls': 0 # New metric
}

# --- Helpers (unchanged except safe hex) ---
def get_expr_value(frame, expr_str, default_value=None):
    val = frame.EvaluateExpression(expr_str)
    if not val.IsValid() or val.GetError().Fail():
        # print_to_lldb_internal(f"    DEBUG: Expr '{expr_str}' failed: {val.GetError().GetCString()}")
        return default_value
    u = val.GetValueAsUnsigned()
    if u is not None and u != 0xffffffffffffffff: # Check for common error/uninit value
        return u
    s = val.GetValueAsSigned()
    if s is not None:
        return s
    # For pointers, GetValueAsUnsigned might be more direct
    # If it's a pointer type, try to get its address value
    if val.GetType().IsPointerType():
        addr = val.GetValueAsUnsigned()
        return addr if addr != 0 else default_value # Return address or default if null

    summary = val.GetSummary()
    if summary:
        return summary
    raw = val.GetValue()
    try:
        return int(raw) # This might fail for complex types
    except:
        return raw or default_value

def safe_hex(v):
    return hex(v) if isinstance(v, int) and v is not None and v != 0 else str(v) # Handle None and 0 better

# Internal print to avoid recursion if print_to_lldb calls a handler
def print_to_lldb_internal(debugger, msg):
    res = lldb.SBCommandReturnObject()
    debugger.GetCommandInterpreter().HandleCommand(f'script print("""{msg}""")', res)
    sys.stdout.flush() # Ensure it's printed immediately

# Keep your original print_to_lldb for general use by handlers
def print_to_lldb(debugger, msg):
    print_to_lldb_internal(debugger, msg)


# --- New: Detach handler ---
def daemon_detach_breakpoint_handler(frame, bp_loc, dict):
    """Fires when attachSharedMemory is called with null pointers."""
    debugger = frame.GetThread().GetProcess().GetTarget().GetDebugger()
    this_addr = get_expr_value(frame, "(void*)this", "N/A")
    print_to_lldb(debugger,
        f"\n*** DETACH SHM on provider {safe_hex(this_addr)} ***\n"
        "Parameters: cb=NULL or ring=NULL\n"
        "Backtrace:")
    debugger.HandleCommand("bt all")
    # return True => stop here, do NOT auto-continue
    return True # Stop here

def daemon_provider_ctor_handler(frame, bp_loc, internal_dict):
    debugger = frame.GetThread().GetProcess().GetTarget().GetDebugger()
    this_addr = get_expr_value(frame, "(void*)this", "N/A")
    print_to_lldb(debugger, f"+++ new IsochPacketProvider @ {safe_hex(this_addr)} +++")
    debugger.HandleCommand("bt 6")
    if os.getenv("LLDB_PAUSE_ON_HIT") != "1":
        frame.GetThread().GetProcess().Continue()
    return os.getenv("LLDB_PAUSE_ON_HIT") == "1"


# --- Breakpoint Handlers ---
def common_continue_or_pause(frame, debugger):
    if os.getenv("LLDB_PAUSE_ON_HIT") == "1":
        print_to_lldb(debugger,
            "  PAUSING (LLDB_PAUSE_ON_HIT=1). Type 'c' to resume.")
        return True # Stop
    frame.GetThread().GetProcess().Continue()
    return False # Continue

# --- NEW BREAKPOINT HANDLER for IsochPortChannelManager::cleanupDispatchers ---
def pcm_cleanup_dispatchers_handler(frame, bp_loc, internal_dict):
    global daemon_metrics
    daemon_metrics['cleanup_dispatchers_calls'] += 1
    debugger = frame.GetThread().GetProcess().GetTarget().GetDebugger()
    output_lines = []

    output_lines.append(f"\n--- PCM_CLEANUP_DISPATCHERS HIT #{daemon_metrics['cleanup_dispatchers_calls']} ---")

    # 'this' pointer of IsochPortChannelManager
    this_addr = get_expr_value(frame, "(void*)this", "N/A")
    output_lines.append(f"  IsochPortChannelManager 'this': {safe_hex(this_addr)}")

    # --- Attempt to inspect relevant member variables ---
    # Note: Member names must exactly match your C++ code.
    #       If they are private, LLDB might need DWARF info or you might need to cast 'this'.
    #       Let's assume they are accessible for now, or adjust casting if needed.
    #       Example: "(static_cast<FWA::Isoch::IsochPortChannelManager*>(this))->m_isochPortRef"

    # Assuming member names like:
    # IOFireWireLibDeviceRef m_deviceInterface; // The overall device
    # IOFireWireLibLocalIsochPortRef m_isochPortRef; // The specific isoch port
    # CFRunLoopRef m_runLoopRef;
    # CFRunLoopSourceRef m_isochDispatcher_RLS; // Or whatever you named the run loop source

    device_interface_addr = get_expr_value(frame, "this->m_deviceInterface", "N/A") # Adjust member name
    isoch_port_ref_addr = get_expr_value(frame, "this->m_isochPortRef", "N/A")       # Adjust member name
    run_loop_ref_addr = get_expr_value(frame, "this->m_runLoopRef", "N/A")             # Adjust member name
    dispatcher_rls_addr = get_expr_value(frame, "this->m_isochDispatcher_RLS", "N/A") # Adjust member name

    output_lines.append(f"  m_deviceInterface: {safe_hex(device_interface_addr)}")
    output_lines.append(f"  m_isochPortRef:    {safe_hex(isoch_port_ref_addr)}")
    output_lines.append(f"  m_runLoopRef:      {safe_hex(run_loop_ref_addr)}")
    output_lines.append(f"  m_isochDispatcher_RLS: {safe_hex(dispatcher_rls_addr)}")

    # Try to get CF retain counts (use with caution, for debugging only)
    # This requires the debugger to be able to call CFGetRetainCount
    if dispatcher_rls_addr not in ["N/A", None, 0]:
        retain_count_str = f"(int)CFGetRetainCount((CFTypeRef){safe_hex(dispatcher_rls_addr)})"
        # print_to_lldb_internal(debugger, f"    DEBUG: Evaluating retain count with: {retain_count_str}")
        rc = get_expr_value(frame, retain_count_str, "RetainCount_Error")
        output_lines.append(f"  Retain count of m_isochDispatcher_RLS: {rc}")

    output_lines.append("  Backtrace (full):")
    for line in output_lines:
        print_to_lldb(debugger, line)

    # Print full backtrace using LLDB's command directly
    debugger.HandleCommand("bt all")
    print_to_lldb(debugger, "--- END PCM_CLEANUP_DISPATCHERS ---")

    # ALWAYS STOP HERE to allow manual inspection before the crash
    print_to_lldb(debugger, "  STOPPING here. Manually 'c' to continue into the likely crash.")
    return True # Stop execution


# --- (rest of your existing handlers, unchanged except minor cleanups) ---
def daemon_pop_breakpoint_handler(frame, bp_loc, internal_dict):
    global daemon_metrics
    daemon_metrics['pop_attempts'] += 1
    debugger = frame.GetThread().GetProcess().GetTarget().GetDebugger()
    output_lines = []

    cb_addr = get_expr_value(frame, "(void*)&cb", "N/A")
    ring_addr = get_expr_value(frame, "(void*)ring", "N/A")
    # out_addr = get_expr_value(frame, "(void*)&out", "N/A") # 'out' not present in your new RTShmRing::pop signature
    # New pop signature: (cb, ring, tsOut, bytesOut, audioPtrOut)
    # We primarily care about cb and ring state here.

    wr_idx = get_expr_value(frame, "RTShmRing::WriteIndexProxy(cb).load(std::memory_order_acquire)", 0)
    rd_idx = get_expr_value(frame, "RTShmRing::ReadIndexProxy(cb).load(std::memory_order_relaxed)", 0)
    abiVersion = get_expr_value(frame, "cb.abiVersion", 0)
    capacity = get_expr_value(frame, "cb.capacity", 0)
    underruns_cpp = get_expr_value(frame, "RTShmRing::UnderrunCountProxy(cb).load(std::memory_order_relaxed)", 0)

    is_empty = (rd_idx == wr_idx)
    seq_ok = False
    shm_seq = 0
    expected_seq = rd_idx + 1
    current_slot_for_pop = 0
    chunk_frames, chunk_data_bytes, chunk_ts_host_time = 0, 0, 0

    if capacity > 0 and not is_empty:
        current_slot_for_pop = rd_idx & (capacity - 1)
        # Make sure 'ring' is valid before trying to dereference it
        if ring_addr not in ["N/A", None, 0]:
             shm_seq_expr = f"RTShmRing::SequenceProxy(ring[{current_slot_for_pop}]).load(std::memory_order_acquire)"
             shm_seq = get_expr_value(frame, shm_seq_expr, "Seq_Error")

             if shm_seq == expected_seq:
                 seq_ok = True
                 daemon_metrics['pops_successful'] += 1
                 chunk_frames = get_expr_value(frame, f"ring[{current_slot_for_pop}].frameCount", 0)
                 chunk_data_bytes = get_expr_value(frame, f"ring[{current_slot_for_pop}].dataBytes", 0)
                 chunk_ts_host_time = get_expr_value(frame, f"ring[{current_slot_for_pop}].timeStamp.mHostTime", 0)
             elif shm_seq != "Seq_Error": # Only count mismatch if we got a value
                 daemon_metrics['pops_failed_seq_mismatch'] += 1
        else:
            output_lines.append("  PY WARN: ring_addr is invalid, cannot get sequence.")
    elif is_empty:
        daemon_metrics['pops_failed_empty'] += 1

    output_lines.append(f"--- DAEMON PYTHON POP ATTEMPT #{daemon_metrics['pop_attempts']} ---")
    output_lines.append(f"  SHM CB Addr: {safe_hex(cb_addr)}, Ring Addr: {safe_hex(ring_addr)}")
    output_lines.append(f"  CB State: ABIVer={abiVersion}, Capacity={capacity}, WR_idx={wr_idx}, RD_idx={rd_idx}, Underruns(C++ SHM)={underruns_cpp}")

    if is_empty:
        output_lines.append(f"  PY PREDICT: FAIL - RING EMPTY. Empty Count (Py): {daemon_metrics['pops_failed_empty']}")
    elif seq_ok:
        output_lines.append(f"  PY PREDICT: SUCCESSFUL POP from slot {current_slot_for_pop}. Seq OK ({shm_seq}). Next RD ~{rd_idx + 1}.")
        output_lines.append(f"    Chunk: Frames={chunk_frames}, Bytes={chunk_data_bytes}, TS.HostTime={chunk_ts_host_time}")
    else:
        output_lines.append(f"  PY PREDICT: FAIL - SEQ MISMATCH slot {current_slot_for_pop}. Expected {expected_seq}, Got {shm_seq}. Mismatch Count (Py): {daemon_metrics['pops_failed_seq_mismatch']}")

    if (daemon_metrics['pop_attempts'] % 200) == 0 or daemon_metrics['pop_attempts'] == 1 : # Log summary less often
        output_lines.append(f"  SUMMARY (Py): Attempts={daemon_metrics['pop_attempts']}, Succeeded={daemon_metrics['pops_successful']}, Empty={daemon_metrics['pops_failed_empty']}, SeqMismatch={daemon_metrics['pops_failed_seq_mismatch']}")
    # output_lines.append("---------------------------------") # Remove to reduce noise
    for line in output_lines: print_to_lldb(debugger, line)
    return common_continue_or_pause(frame, debugger)


def daemon_getbytes_breakpoint_handler(frame, bp_loc, internal_dict):
    # This handler seems to be for an older version of your IsochPacketProvider
    # as it refers to m_shmAccess_. Your current IsochPacketProvider uses direct
    # shmControlBlock_ and shmRingArray_ pointers.
    # If you still need it, it needs to be updated to reflect the current class structure.
    # For now, I'll keep it but note it might be outdated.
    global daemon_metrics
    daemon_metrics['getbytes_calls'] += 1
    debugger = frame.GetThread().GetProcess().GetTarget().GetDebugger()
    print_to_lldb(debugger, f"--- DAEMON PYTHON IsochPacketProvider::getBytes (outdated?) CALLED #{daemon_metrics['getbytes_calls']} ---")
    # ... (rest of the logic, but verify member names like m_shmAccess_)
    return common_continue_or_pause(frame, debugger)

def daemon_fillpacketdata_breakpoint_handler(frame, bp_loc, internal_dict):
    global daemon_metrics
    daemon_metrics['fillpacketdata_calls'] += 1
    debugger = frame.GetThread().GetProcess().GetTarget().GetDebugger()
    output_lines = []

    this_addr = get_expr_value(frame, "(void*)this", "N/A")
    targetBuffer_addr = get_expr_value(frame, "(void*)targetBuffer", "N/A")
    targetBufferSize_val = get_expr_value(frame, "targetBufferSize", 0)

    info_segmentIndex = get_expr_value(frame, "info.segmentIndex", 0)
    info_packetIndexInGroup = get_expr_value(frame, "info.packetIndexInGroup", 0)
    # info_dataByteCount = get_expr_value(frame, "info.dataByteCount", 0) # dataByteCount is not in TransmitPacketInfo
    # info_sytOffset = get_expr_value(frame, "info.sytOffset", 0)         # sytOffset is not in TransmitPacketInfo
    # Referencing TransmitPacketInfo from AmdtpTransmitter.hpp
    info_absPktIdx = get_expr_value(frame, "info.absolutePacketIndex", 0)
    info_hostTsNano = get_expr_value(frame, "info.hostTimestampNano", 0)
    info_fwTs = get_expr_value(frame, "info.firewireTimestamp", 0)


    output_lines.append(f"--- DAEMON PYTHON fillPacketData CALLED #{daemon_metrics['fillpacketdata_calls']} ---")
    output_lines.append(f"  this={safe_hex(this_addr)}, targetBuffer={safe_hex(targetBuffer_addr)}, targetBufferSize={targetBufferSize_val}")
    output_lines.append(f"  Info: segIdx={info_segmentIndex}, pktInGrp={info_packetIndexInGroup}, absPktIdx={info_absPktIdx}, hostTs={info_hostTsNano}, fwTs=0x{info_fwTs:X}")

    if (daemon_metrics['fillpacketdata_calls'] % 200 == 0) or daemon_metrics['fillpacketdata_calls'] == 1: # Log less often
        for line in output_lines: print_to_lldb(debugger, line)
        print_to_lldb(debugger, "  Backtrace (top 6):")
        debugger.HandleCommand("bt 6")
        print_to_lldb(debugger, "---------------------------------")
    elif daemon_metrics['fillpacketdata_calls'] < 10: # Log first few calls
        for line in output_lines: print_to_lldb(debugger, line)


    return common_continue_or_pause(frame, debugger)