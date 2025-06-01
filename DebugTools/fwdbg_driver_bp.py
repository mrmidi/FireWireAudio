# fwdbg_driver_bp.py
import lldb
import shlex # For safely splitting command strings if needed, though not strictly here.

# LLDB Persistent-like variables (managed as globals in this Python module scope)
# These will reset each time the Python script is loaded by LLDB (i.e., each new lldb session)
driver_metrics = {
    'push_attempts': 0,
    'pushes_successful': 0,
    'pushes_failed_ring_full': 0,
    'pushes_failed_bad_params': 0
}

def get_expr_value(frame, expression_str):
    """Helper to evaluate an expression and return its value or None."""
    val = frame.EvaluateExpression(expression_str)
    if val.IsValid() and val.GetValueAsUnsigned() is not None:
        return val.GetValueAsUnsigned()
    elif val.IsValid() and val.GetValue() is not None: # For pointers or other types
        return val.GetValue()
    return None

def get_expr_value_signed(frame, expression_str):
    """Helper to evaluate an expression and return its signed value or None."""
    val = frame.EvaluateExpression(expression_str)
    if val.IsValid() and val.GetValueAsSigned() is not None:
        return val.GetValueAsSigned()
    return None

def driver_push_breakpoint_handler(frame, bp_loc, internal_dict):
    """
    This function is called when the RTShmRing::push breakpoint is hit in the driver.
    frame: SBFrame object for the current frame.
    bp_loc: SBBreakpointLocation object.
    internal_dict: A Python dictionary that LLDB preserves between calls to this function
                   for the same breakpoint. You can use this for state that needs to
                   persist across hits for a *single breakpoint instance*.
                   For simple global-like counters across all push calls, module globals are easier.
    """
    global driver_metrics
    driver_metrics['push_attempts'] += 1

    # --- Access function parameters using SBFrame API ---
    # This is more robust than relying on expr with direct names,
    # though expr often works for simple cases.
    # args = frame.GetVariables(True, False, False, False) # arguments=True, locals=False, statics=False, in_scope_only=False
    # cb_val = args.GetValueForVariablePath("cb")
    # ring_val = args.GetValueForVariablePath("ring")
    # frames_val = args.GetValueForVariablePath("frames")
    # bytesPerFrame_val = args.GetValueForVariablePath("bytesPerFrame")
    # src_val = args.GetValueForVariablePath("src")
    # ts_val = args.GetValueForVariablePath("ts")

    # For simplicity and given that `expr` with param names usually works,
    # we can still use it here, but be mindful it *can* be fragile.
    # Using frame.EvaluateExpression is generally safer.

    cb_addr = get_expr_value(frame, "(void*)&cb")
    ring_addr = get_expr_value(frame, "(void*)ring")
    frames = get_expr_value(frame, "frames")
    bytesPerFrame = get_expr_value(frame, "bytesPerFrame")
    src_addr = get_expr_value(frame, "(void*)src")
    # ts_host_time = get_expr_value(frame, "ts.mHostTime") # ts is a reference, access members directly

    # Evaluate conditions directly (these expressions run in LLDB's C++ expression parser)
    is_ring_full_val = frame.EvaluateExpression("RTShmRing::WriteIndexProxy(cb).load(std::memory_order_relaxed) - RTShmRing::ReadIndexProxy(cb).load(std::memory_order_acquire) >= cb.capacity")
    are_params_ok_val = frame.EvaluateExpression("src != nullptr && ring != nullptr && frames > 0 && frames <= kMaxFramesPerChunk && (frames * bytesPerFrame) <= kAudioDataBytes")

    is_ring_full = is_ring_full_val.GetValueAsUnsigned() != 0 if is_ring_full_val.IsValid() else True # Assume full on error
    are_params_ok = are_params_ok_val.GetValueAsUnsigned() != 0 if are_params_ok_val.IsValid() else False # Assume not ok on error

    will_be_successful_push = False
    if are_params_ok and not is_ring_full:
        driver_metrics['pushes_successful'] += 1
        will_be_successful_push = True
    if is_ring_full:
        driver_metrics['pushes_failed_ring_full'] += 1
    if not are_params_ok: # Check this even if ring is full, as params could also be bad
        driver_metrics['pushes_failed_bad_params'] += 1


    # --- Get current SHM state for printing ---
    # These will be evaluated in the context of the breakpoint
    abiVersion = get_expr_value(frame, "cb.abiVersion")
    capacity = get_expr_value(frame, "cb.capacity")
    wr_idx = get_expr_value(frame, "RTShmRing::WriteIndexProxy(cb).load(std::memory_order_relaxed)")
    rd_idx = get_expr_value(frame, "RTShmRing::ReadIndexProxy(cb).load(std::memory_order_acquire)")
    overruns_cpp = get_expr_value(frame, "RTShmRing::OverrunCountProxy(cb).load(std::memory_order_relaxed)")
    ts_host_time = get_expr_value(frame, "ts.mHostTime")


    # --- Print Information ---
    # Use lldb.debugger.HandleCommand or result.AppendMessage for output
    result = lldb.SBCommandReturnObject()
    debugger = lldb.debugger # Or frame.GetThread().GetProcess().GetTarget().GetDebugger()

    output = []
    output.append(f"--- DRIVER PYTHON PUSH ATTEMPT #{driver_metrics['push_attempts']} ---")
    output.append(f"  Func Params: frames={frames}, bytesPerFrame={bytesPerFrame}, src=0x{src_addr:x}, ts.mHostTime={ts_host_time}")
    output.append(f"  SHM CB Addr: 0x{cb_addr:x}, Ring Addr: 0x{ring_addr:x}")
    output.append(f"  CB State: ABIVer={abiVersion}, Capacity={capacity}, WR_idx={wr_idx}, RD_idx={rd_idx}, Overruns(C++ SHM)={overruns_cpp}")

    if will_be_successful_push:
        output.append(f"  PYTHON PREDICT: SUCCESSFUL PUSH. Next WR will be ~{wr_idx + 1}.")
    if is_ring_full:
        output.append(f"  PYTHON PREDICT: FAIL - RING FULL. Overrun Count (LLDB Py): {driver_metrics['pushes_failed_ring_full']}")
    if not are_params_ok:
        output.append(f"  PYTHON PREDICT: FAIL - BAD PARAMS. Bad Param Count (LLDB Py): {driver_metrics['pushes_failed_bad_params']}")

    if (driver_metrics['push_attempts'] & 0xFFF) == 0 or driver_metrics['push_attempts'] == 1:
        output.append(f"  SUMMARY (LLDB Py): Attempts={driver_metrics['push_attempts']}, Succeeded={driver_metrics['pushes_successful']}, Full={driver_metrics['pushes_failed_ring_full']}, BadParams={driver_metrics['pushes_failed_bad_params']}")
    output.append("--------------------------------")

    for line in output:
        debugger.HandleCommand(f"platform shell echo '{line}'") # A bit clunky for multiline, direct print better
        # Or, accumulate and print once, or use lldb.SBCommandReturnObject
        # print(line) # This will go to LLDB's script output, usually visible

    # To continue execution:
    frame.GetThread().GetProcess().Continue()
    return False # Indicates that LLDB should not stop. If True, it would stop.
                 # For breakpoint commands, this return value is often ignored if you manage continue yourself.
                 # For a Python function directly as a breakpoint's action (without `breakpoint command add`),
                 # returning False means "don't stop", True means "stop".
                 # Here, since we will use `breakpoint command add X -F module.function`,
                 # the Python function should typically manage the continue.