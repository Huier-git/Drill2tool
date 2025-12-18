# -*- coding: utf-8 -*-
from ortools.sat.python import cp_model
import collections
import time
import argparse
import json
import sys
import os

# ==========================================
# 1. 基础数据定义
# ==========================================

DOFS = ["Fz", "Sr", "Me", "Mg", "Mr", "Dh", "Pr", "Cb"]

# 默认时长配置
DEFAULT_DUR = {
    # --- Stage A: Install Drill Tool ---
    "A_FZ_AH": 8, "A_ME_to_store": 3, "A_MG_grip": 3, "A_ME_back": 3,
    "A_MR_to_head": 3, "A_ME_to_head": 3, "A_FZ_HG": 5,
    "A_COUPLE_GE": 6, "A_DH_lock": 1, "A_MG_release": 2,
    "A_ME_back_from_head": 3, "A_MR_back_to_store": 3,
    "A_DRILL": 10, "A_CB_clamp": 5, "A_DH_unlock": 1,
    "A_BREAK_AC": 6, "A_FZ_CH": 7,

    # --- Stage B: Install Rods & Drill ---
    "SR_INDEX": 3,
    "B_ME_to_store": 3, "B_MG_grip": 3, "B_ME_back": 3,
    "B_MR_to_head": 3, "B_ME_to_head": 3,
    "B_FZ_HF": 4, "B_COUPLE_FD": 6,
    "B_DH_lock": 1, "B_MG_release": 2,
    "B_ME_back_from_head": 3, "B_MR_back_to_store": 3,
    "B_FZ_DJ": 4, "B_COUPLE_JI": 6,
    "B_CB_release": 5, "B_DRILL": 10, "B_CB_clamp": 5,
    "B_DH_unlock": 1, "B_BREAK_AC": 6, "B_FZ_CH": 7,

    # --- Stage C: Retrieve Rods ---
    "C_FZ_HC": 6, "C_COUPLE_CB": 6, "C_DH_lock": 1,
    "C_CB_release": 5, "C_FZ_BI": 8, "C_CB_clamp": 5,
    "C_BREAK_IJ": 6, "C_FZ_JD": 5,
    "C_MR_Assist": 3, "C_ME_Assist": 3, "C_MG_Grip": 3,
    "C_DH_unlock": 1, "C_BREAK_DF": 6, "C_FZ_FH": 7,
    "C_ME_Retract": 3, "C_MR_Retract": 3,
    "C_ME_Store": 3, "C_MG_Release": 2, "C_ME_Back": 3,
    "C_SR_Next": 3,

    # --- Stage D: Retrieve Tool (Disassembly) ---
    "D_FZ_HC": 6, "D_COUPLE_CB": 6, "D_DH_lock": 1,
    "D_CB_release": 5, "D_FZ_BE": 6,
    "D_MR_Assist": 3, "D_ME_Assist": 3, "D_MG_Grip": 3,
    "D_DH_unlock": 1, "D_BREAK_EG": 6, "D_FZ_GH": 7,
    "D_SR_Reset": 3,
    "D_ME_Retract": 3, "D_MR_Retract": 3,
    "D_ME_Store": 3, "D_MG_Release": 2, "D_ME_Back": 3
}

# 全局DUR变量，可被外部配置覆盖
DUR = DEFAULT_DUR.copy()

def load_durations_from_file(config_path: str):
    """从JSON文件加载时长配置"""
    global DUR
    if os.path.exists(config_path):
        with open(config_path, 'r', encoding='utf-8') as f:
            custom_dur = json.load(f)
            for key, value in custom_dur.items():
                if key in DUR:
                    DUR[key] = value
        print(f"[INFO] Loaded custom durations from: {config_path}")


# ==========================================
# 2. 调度器核心
# ==========================================

class RigScheduler:
    def __init__(self):
        self.model = cp_model.CpModel()
        self.tasks = {}
        self.machine_intervals = collections.defaultdict(list)
        self.task_details = {}
        self.horizon = 2000  # 扩大时间视窗以容纳全流程
        self.stage_cuts = []
        self.serial_duration = 0

    def add_task(self, name, dof, duration, start_state, end_state, op_type="move", middle_state=None,
                 is_sync_duplicate=False):
        start_var = self.model.NewIntVar(0, self.horizon, f'start_{name}')
        end_var = self.model.NewIntVar(0, self.horizon, f'end_{name}')
        interval_var = self.model.NewIntervalVar(start_var, duration, end_var, f'interval_{name}')

        self.tasks[name] = (start_var, end_var, interval_var)
        self.machine_intervals[dof].append(interval_var)
        self.task_details[name] = {
            "dof": dof, "dur": duration,
            "s_val": start_state, "e_val": end_state,
            "type": op_type,
            "mid_val": middle_state if middle_state else start_state
        }

        if not is_sync_duplicate:
            self.serial_duration += duration

        return name

    def add_precedence(self, task_before, task_after):
        if task_before in self.tasks and task_after in self.tasks:
            self.model.Add(self.tasks[task_after][0] >= self.tasks[task_before][1])

    def add_synchronization(self, task_a, task_b):
        s1, e1, _ = self.tasks[task_a]
        s2, e2, _ = self.tasks[task_b]
        self.model.Add(s1 == s2)
        self.model.Add(e1 == e2)

    def add_safety_delay(self, trigger_task, dependent_task, delay):
        s_trigger = self.tasks[trigger_task][0]
        s_dependent = self.tasks[dependent_task][0]
        self.model.Add(s_dependent >= s_trigger + delay)

    def get_task_end_var(self, name):
        return self.tasks[name][1]

    def solve(self):
        for dof, intervals in self.machine_intervals.items():
            self.model.AddNoOverlap(intervals)

        makespan = self.model.NewIntVar(0, self.horizon, 'makespan')
        all_ends = [t[1] for t in self.tasks.values()]
        self.model.AddMaxEquality(makespan, all_ends)
        self.model.Minimize(makespan)

        solver = cp_model.CpSolver()
        print(">>> Solver started...")
        t_start = time.perf_counter()
        status = solver.Solve(self.model)
        t_end = time.perf_counter()

        wall_time = (t_end - t_start) * 1000.0

        if status == cp_model.OPTIMAL or status == cp_model.FEASIBLE:
            optimized_duration = solver.Value(makespan)
            print(f"\n=== Result Found ===")
            print(f"Solver Wall Time  : {wall_time:.4f} ms")
            print(f"Strict Serial Time: {self.serial_duration} s")
            print(f"Optimized Time    : {optimized_duration} s")
            print(f"Efficiency Gain   : {self.serial_duration - optimized_duration} s (Saved!)")
            return solver, optimized_duration
        else:
            print("No solution found.")
            return None, 0


# ==========================================
# 3. 业务逻辑定义 (Stage A, B, C, D)
# ==========================================

def define_stage_A(S: RigScheduler):
    """ Stage A: Install Drill Tool """
    p = "A_"
    S.add_task(f"{p}Fz_Lift", "Fz", DUR["A_FZ_AH"], "A", "H")
    S.add_task(f"{p}Fz_Adjust", "Fz", DUR["A_FZ_HG"], "H", "G")
    S.add_task(f"{p}Me_Store", "Me", DUR["A_ME_to_store"], "A", "B")
    S.add_task(f"{p}Mg_Grip", "Mg", DUR["A_MG_grip"], "A", "B", op_type="move")
    S.add_task(f"{p}Me_Back", "Me", DUR["A_ME_back"], "B", "A")
    S.add_task(f"{p}Me_Head", "Me", DUR["A_ME_to_head"], "A", "C")
    S.add_task(f"{p}Couple_Fz", "Fz", DUR["A_COUPLE_GE"], "G", "E")
    S.add_task(f"{p}Couple_Pr", "Pr", DUR["A_COUPLE_GE"], "A", "A", op_type="spin", middle_state="B",
               is_sync_duplicate=True)
    S.add_task(f"{p}Dh_Lock", "Dh", DUR["A_DH_lock"], "A", "B")
    S.add_task(f"{p}Mg_Open", "Mg", DUR["A_MG_release"], "B", "A", op_type="move")
    S.add_task(f"{p}Me_Retract", "Me", DUR["A_ME_back_from_head"], "C", "A")
    S.add_task(f"{p}Drill_Fz", "Fz", DUR["A_DRILL"], "E", "A")
    S.add_task(f"{p}Drill_Pr", "Pr", DUR["A_DRILL"], "A", "A", op_type="spin", middle_state="D", is_sync_duplicate=True)
    S.add_task(f"{p}Cb_Clamp", "Cb", DUR["A_CB_clamp"], "A", "B")
    S.add_task(f"{p}Dh_Unlock", "Dh", DUR["A_DH_unlock"], "B", "A")
    S.add_task(f"{p}Break_Fz", "Fz", DUR["A_BREAK_AC"], "A", "C")
    S.add_task(f"{p}Break_Pr", "Pr", DUR["A_BREAK_AC"], "A", "A", op_type="spin", middle_state="C",
               is_sync_duplicate=True)
    last_task = S.add_task(f"{p}Fz_Reset", "Fz", DUR["A_FZ_CH"], "C", "H")

    # Constraints
    S.add_precedence(f"{p}Me_Store", f"{p}Mg_Grip")
    S.add_precedence(f"{p}Mg_Grip", f"{p}Me_Back")
    S.add_precedence(f"{p}Me_Back", f"{p}Me_Head")
    S.add_precedence(f"{p}Me_Head", f"{p}Couple_Fz")
    S.add_precedence(f"{p}Mg_Grip", f"{p}Mg_Open")
    S.add_synchronization(f"{p}Couple_Fz", f"{p}Couple_Pr")
    S.add_synchronization(f"{p}Drill_Fz", f"{p}Drill_Pr")
    S.add_synchronization(f"{p}Break_Fz", f"{p}Break_Pr")
    S.add_precedence(f"{p}Fz_Lift", f"{p}Fz_Adjust")
    S.add_precedence(f"{p}Fz_Adjust", f"{p}Couple_Fz")
    S.add_precedence(f"{p}Couple_Fz", f"{p}Dh_Lock")
    S.add_precedence(f"{p}Dh_Lock", f"{p}Mg_Open")
    S.add_precedence(f"{p}Mg_Open", f"{p}Me_Retract")
    S.add_precedence(f"{p}Me_Retract", f"{p}Drill_Fz")
    S.add_precedence(f"{p}Drill_Fz", f"{p}Cb_Clamp")
    S.add_precedence(f"{p}Cb_Clamp", f"{p}Dh_Unlock")
    S.add_precedence(f"{p}Dh_Unlock", f"{p}Break_Fz")
    S.add_precedence(f"{p}Break_Fz", f"{p}Fz_Reset")
    safety_delay = int(DUR["A_FZ_AH"] * 0.75)
    S.add_safety_delay(f"{p}Fz_Lift", f"{p}Me_Head", safety_delay)
    return S.get_task_end_var(last_task)


def define_stage_B(S: RigScheduler, index: int, total_n: int, start_after_var):
    """ Stage B: Add Rod & Drill """
    p = f"B{index}_"
    first_task = S.add_task(f"{p}Sr_Index", "Sr", DUR["SR_INDEX"], f"{index - 1}", f"{index}")
    S.add_task(f"{p}Me_Store", "Me", DUR["B_ME_to_store"], "A", "B")
    S.add_task(f"{p}Mg_Grip", "Mg", DUR["B_MG_grip"], "A", "B", op_type="move")
    S.add_task(f"{p}Me_Back", "Me", DUR["B_ME_back"], "B", "A")
    S.add_task(f"{p}Mr_Head", "Mr", DUR["B_MR_to_head"], "A", "B")
    S.add_task(f"{p}Me_Head", "Me", DUR["B_ME_to_head"], "A", "C")
    S.add_task(f"{p}Fz_HF", "Fz", DUR["B_FZ_HF"], "H", "F")
    S.add_task(f"{p}Couple_FD_Fz", "Fz", DUR["B_COUPLE_FD"], "F", "D")
    S.add_task(f"{p}Couple_FD_Pr", "Pr", DUR["B_COUPLE_FD"], "A", "A", op_type="spin", middle_state="B",
               is_sync_duplicate=True)
    S.add_task(f"{p}Dh_Lock", "Dh", DUR["B_DH_lock"], "A", "B")
    S.add_task(f"{p}Mg_Release", "Mg", DUR["B_MG_release"], "B", "A", op_type="move")
    S.add_task(f"{p}Me_Retract", "Me", DUR["B_ME_back_from_head"], "C", "A")
    S.add_task(f"{p}Mr_Retract", "Mr", DUR["B_MR_back_to_store"], "B", "A")
    S.add_task(f"{p}Fz_DJ", "Fz", DUR["B_FZ_DJ"], "D", "J")
    S.add_task(f"{p}Couple_JI_Fz", "Fz", DUR["B_COUPLE_JI"], "J", "I")
    S.add_task(f"{p}Couple_JI_Pr", "Pr", DUR["B_COUPLE_JI"], "A", "A", op_type="spin", middle_state="B",
               is_sync_duplicate=True)
    S.add_task(f"{p}Cb_Release", "Cb", DUR["B_CB_release"], "B", "A")
    S.add_task(f"{p}Drill_Fz", "Fz", DUR["B_DRILL"], "I", "A")
    S.add_task(f"{p}Drill_Pr", "Pr", DUR["B_DRILL"], "A", "A", op_type="spin", middle_state="D", is_sync_duplicate=True)
    S.add_task(f"{p}Cb_Clamp", "Cb", DUR["B_CB_clamp"], "A", "B")
    S.add_task(f"{p}Dh_Unlock", "Dh", DUR["B_DH_unlock"], "B", "A")
    S.add_task(f"{p}Break_Fz", "Fz", DUR["B_BREAK_AC"], "A", "C")
    S.add_task(f"{p}Break_Pr", "Pr", DUR["B_BREAK_AC"], "A", "A", op_type="spin", middle_state="C",
               is_sync_duplicate=True)
    last_task = S.add_task(f"{p}Fz_Reset", "Fz", DUR["B_FZ_CH"], "C", "H")

    # Constraints
    S.model.Add(S.tasks[f"{p}Sr_Index"][0] >= start_after_var)
    S.model.Add(S.tasks[f"{p}Me_Store"][0] >= start_after_var)
    S.model.Add(S.tasks[f"{p}Fz_HF"][0] >= start_after_var)
    S.add_precedence(f"{p}Sr_Index", f"{p}Me_Store")
    S.add_precedence(f"{p}Me_Store", f"{p}Mg_Grip")
    S.add_precedence(f"{p}Mg_Grip", f"{p}Me_Back")
    S.add_precedence(f"{p}Me_Back", f"{p}Mr_Head")
    S.add_precedence(f"{p}Mr_Head", f"{p}Me_Head")
    S.add_precedence(f"{p}Me_Head", f"{p}Couple_FD_Fz")
    S.add_precedence(f"{p}Mg_Grip", f"{p}Mg_Release")
    S.add_synchronization(f"{p}Couple_FD_Fz", f"{p}Couple_FD_Pr")
    S.add_synchronization(f"{p}Couple_JI_Fz", f"{p}Couple_JI_Pr")
    S.add_synchronization(f"{p}Drill_Fz", f"{p}Drill_Pr")
    S.add_synchronization(f"{p}Break_Fz", f"{p}Break_Pr")
    S.add_precedence(f"{p}Fz_HF", f"{p}Couple_FD_Fz")
    S.add_precedence(f"{p}Couple_FD_Fz", f"{p}Dh_Lock")
    S.add_precedence(f"{p}Dh_Lock", f"{p}Mg_Release")
    S.add_precedence(f"{p}Mg_Release", f"{p}Me_Retract")
    S.add_precedence(f"{p}Mg_Release", f"{p}Fz_DJ")
    S.add_precedence(f"{p}Me_Retract", f"{p}Mr_Retract")
    S.add_precedence(f"{p}Me_Retract", f"{p}Drill_Fz")
    S.add_precedence(f"{p}Fz_DJ", f"{p}Couple_JI_Fz")
    S.add_precedence(f"{p}Couple_JI_Fz", f"{p}Cb_Release")
    S.add_precedence(f"{p}Cb_Release", f"{p}Drill_Fz")
    S.add_precedence(f"{p}Drill_Fz", f"{p}Cb_Clamp")
    S.add_precedence(f"{p}Cb_Clamp", f"{p}Dh_Unlock")
    S.add_precedence(f"{p}Dh_Unlock", f"{p}Break_Fz")
    S.add_precedence(f"{p}Break_Fz", f"{p}Fz_Reset")
    return S.get_task_end_var(last_task)


def define_stage_C(S: RigScheduler, index: int, total_n: int, start_after_var):
    """ Stage C: Retrieve Rod """
    p = f"C{index}_"
    S.add_task(f"{p}Fz_HC", "Fz", DUR["C_FZ_HC"], "H", "C")
    S.add_task(f"{p}Couple_CB_Fz", "Fz", DUR["C_COUPLE_CB"], "C", "B")
    S.add_task(f"{p}Couple_CB_Pr", "Pr", DUR["C_COUPLE_CB"], "A", "A", op_type="spin", middle_state="B",
               is_sync_duplicate=True)
    S.add_task(f"{p}Dh_Lock", "Dh", DUR["C_DH_lock"], "A", "B")
    S.add_task(f"{p}Cb_Open", "Cb", DUR["C_CB_release"], "B", "A")
    S.add_task(f"{p}Lift_Rod", "Fz", DUR["C_FZ_BI"], "B", "I")
    S.add_task(f"{p}Cb_Clamp", "Cb", DUR["C_CB_clamp"], "A", "B")
    S.add_task(f"{p}Break_Lower_Fz", "Fz", DUR["C_BREAK_IJ"], "I", "J")
    S.add_task(f"{p}Break_Lower_Pr", "Pr", DUR["C_BREAK_IJ"], "A", "A", op_type="spin", middle_state="C",
               is_sync_duplicate=True)
    S.add_task(f"{p}Fz_JD", "Fz", DUR["C_FZ_JD"], "J", "D")
    S.add_task(f"{p}Mr_Assist", "Mr", DUR["C_MR_Assist"], "A", "B")
    S.add_task(f"{p}Me_Assist", "Me", DUR["C_ME_Assist"], "A", "C")
    S.add_task(f"{p}Mg_Grip", "Mg", DUR["C_MG_Grip"], "A", "B", op_type="move")
    S.add_task(f"{p}Dh_Unlock", "Dh", DUR["C_DH_unlock"], "B", "A")
    S.add_task(f"{p}Break_Upper_Fz", "Fz", DUR["C_BREAK_DF"], "D", "F")
    S.add_task(f"{p}Break_Upper_Pr", "Pr", DUR["C_BREAK_DF"], "A", "A", op_type="spin", middle_state="C",
               is_sync_duplicate=True)
    S.add_task(f"{p}Fz_Reset", "Fz", DUR["C_FZ_FH"], "F", "H")
    S.add_task(f"{p}Me_Retract", "Me", DUR["C_ME_Retract"], "C", "A")
    S.add_task(f"{p}Mr_Retract", "Mr", DUR["C_MR_Retract"], "B", "A")
    S.add_task(f"{p}Me_Store", "Me", DUR["C_ME_Store"], "A", "B")
    S.add_task(f"{p}Mg_Release", "Mg", DUR["C_MG_Release"], "B", "A", op_type="move")
    last_task = S.add_task(f"{p}Me_Back", "Me", DUR["C_ME_Back"], "B", "A")
    S.add_task(f"{p}Sr_Back", "Sr", DUR["C_SR_Next"], f"{index}", f"{index - 1}")

    # Constraints
    S.model.Add(S.tasks[f"{p}Fz_HC"][0] >= start_after_var)
    S.add_precedence(f"{p}Fz_HC", f"{p}Couple_CB_Fz")
    S.add_synchronization(f"{p}Couple_CB_Fz", f"{p}Couple_CB_Pr")
    S.add_precedence(f"{p}Couple_CB_Fz", f"{p}Dh_Lock")
    S.add_precedence(f"{p}Dh_Lock", f"{p}Cb_Open")
    S.add_precedence(f"{p}Cb_Open", f"{p}Lift_Rod")
    S.add_precedence(f"{p}Lift_Rod", f"{p}Cb_Clamp")
    S.add_precedence(f"{p}Cb_Clamp", f"{p}Break_Lower_Fz")
    S.add_synchronization(f"{p}Break_Lower_Fz", f"{p}Break_Lower_Pr")
    S.add_precedence(f"{p}Break_Lower_Fz", f"{p}Fz_JD")
    S.add_precedence(f"{p}Fz_JD", f"{p}Mr_Assist")
    S.add_precedence(f"{p}Mr_Assist", f"{p}Me_Assist")
    S.add_precedence(f"{p}Me_Assist", f"{p}Mg_Grip")
    S.add_precedence(f"{p}Mg_Grip", f"{p}Dh_Unlock")
    S.add_precedence(f"{p}Dh_Unlock", f"{p}Break_Upper_Fz")
    S.add_synchronization(f"{p}Break_Upper_Fz", f"{p}Break_Upper_Pr")
    S.add_precedence(f"{p}Break_Upper_Fz", f"{p}Fz_Reset")
    S.add_precedence(f"{p}Break_Upper_Fz", f"{p}Me_Retract")
    S.add_precedence(f"{p}Me_Retract", f"{p}Mr_Retract")
    S.add_precedence(f"{p}Mr_Retract", f"{p}Me_Store")
    S.add_precedence(f"{p}Me_Store", f"{p}Mg_Release")
    S.add_precedence(f"{p}Mg_Release", f"{p}Me_Back")
    S.add_precedence(f"{p}Me_Back", f"{p}Sr_Back")
    return S.get_task_end_var(last_task)


def define_stage_D(S: RigScheduler, start_after_var):
    """ Stage D: Retrieve Tool (Final) """
    p = "D_"

    # 1. Fz Move to Couple Tool
    S.add_task(f"{p}Fz_HC", "Fz", DUR["D_FZ_HC"], "H", "C")
    S.add_task(f"{p}Couple_CB_Fz", "Fz", DUR["D_COUPLE_CB"], "C", "B")
    S.add_task(f"{p}Couple_CB_Pr", "Pr", DUR["D_COUPLE_CB"], "A", "A", op_type="spin", middle_state="B",
               is_sync_duplicate=True)

    # 2. Lock Upper, Release Lower
    S.add_task(f"{p}Dh_Lock", "Dh", DUR["D_DH_lock"], "A", "B")
    S.add_task(f"{p}Cb_Open", "Cb", DUR["D_CB_release"], "B", "A")

    # 3. Lift Tool
    S.add_task(f"{p}Lift_Tool", "Fz", DUR["D_FZ_BE"], "B", "E")

    # 4. Arms Enter (Me/Mr/Mg)
    S.add_task(f"{p}Mr_Assist", "Mr", DUR["D_MR_Assist"], "A", "B")
    S.add_task(f"{p}Me_Assist", "Me", DUR["D_ME_Assist"], "A", "C")
    S.add_task(f"{p}Mg_Grip", "Mg", DUR["D_MG_Grip"], "A", "B", op_type="move")

    # 5. Unlock Upper, Break Tool
    S.add_task(f"{p}Dh_Unlock", "Dh", DUR["D_DH_unlock"], "B", "A")
    S.add_task(f"{p}Break_Tool_Fz", "Fz", DUR["D_BREAK_EG"], "E", "G")
    S.add_task(f"{p}Break_Tool_Pr", "Pr", DUR["D_BREAK_EG"], "A", "A", op_type="spin", middle_state="C",
               is_sync_duplicate=True)

    # 6. Fz Reset
    S.add_task(f"{p}Fz_Reset", "Fz", DUR["D_FZ_GH"], "G", "H")

    # 7. Retrieve Tool to Store
    S.add_task(f"{p}Me_Retract", "Me", DUR["D_ME_Retract"], "C", "A")
    S.add_task(f"{p}Mr_Retract", "Mr", DUR["D_MR_Retract"], "B", "A")
    S.add_task(f"{p}Me_Store", "Me", DUR["D_ME_Store"], "A", "B")
    S.add_task(f"{p}Mg_Release", "Mg", DUR["D_MG_Release"], "B", "A", op_type="move")
    last_task = S.add_task(f"{p}Me_Back", "Me", DUR["D_ME_Back"], "B", "A")

    # 8. Sr Reset
    S.add_task(f"{p}Sr_Reset", "Sr", DUR["D_SR_Reset"], "B", "A")

    # --- Constraints ---
    S.model.Add(S.tasks[f"{p}Fz_HC"][0] >= start_after_var)

    S.add_precedence(f"{p}Fz_HC", f"{p}Couple_CB_Fz")
    S.add_synchronization(f"{p}Couple_CB_Fz", f"{p}Couple_CB_Pr")

    S.add_precedence(f"{p}Couple_CB_Fz", f"{p}Dh_Lock")
    S.add_precedence(f"{p}Dh_Lock", f"{p}Cb_Open")
    S.add_precedence(f"{p}Cb_Open", f"{p}Lift_Tool")

    # Mr enter early optimization: Lift 完成前，Mr 可以到位 (只要不撞)
    # 假设 Lift 到 E 位后，Me 才能进，但 Mr (A->B) 可能不干涉？
    # 按照严格逻辑：Lift 完成 -> Me/Mr 进
    S.add_precedence(f"{p}Lift_Tool", f"{p}Mr_Assist")
    S.add_precedence(f"{p}Mr_Assist", f"{p}Me_Assist")
    S.add_precedence(f"{p}Me_Assist", f"{p}Mg_Grip")

    S.add_precedence(f"{p}Mg_Grip", f"{p}Dh_Unlock")
    S.add_precedence(f"{p}Dh_Unlock", f"{p}Break_Tool_Fz")
    S.add_synchronization(f"{p}Break_Tool_Fz", f"{p}Break_Tool_Pr")

    # Parallel Branch: Fz Reset & Me Retrieve
    S.add_precedence(f"{p}Break_Tool_Fz", f"{p}Fz_Reset")
    S.add_precedence(f"{p}Break_Tool_Fz", f"{p}Me_Retract")

    # Strictly Serial: Me Retract -> Mr Retract -> Me Store
    S.add_precedence(f"{p}Me_Retract", f"{p}Mr_Retract")
    S.add_precedence(f"{p}Mr_Retract", f"{p}Me_Store")
    S.add_precedence(f"{p}Me_Store", f"{p}Mg_Release")
    S.add_precedence(f"{p}Mg_Release", f"{p}Me_Back")

    # Sr Reset trigger
    S.add_precedence(f"{p}Me_Back", f"{p}Sr_Reset")

    return S.get_task_end_var(last_task)


# ==========================================
# 4. ASCII 可视化 (支持 Zoom)
# ==========================================

def render_ascii_gantt(solver, scheduler, total_makespan, zoom_factor=1):
    results = []
    max_time_scaled = int(total_makespan * zoom_factor) + 2

    cut_points = set()
    for var in scheduler.stage_cuts:
        t = solver.Value(var)
        cut_points.add(int(t * zoom_factor))

    for name, (s_var, e_var, _) in scheduler.tasks.items():
        start = solver.Value(s_var)
        end = solver.Value(e_var)
        details = scheduler.task_details[name]

        s_scaled = int(start * zoom_factor)
        e_scaled = int(end * zoom_factor)
        results.append({
            's': s_scaled, 'e': e_scaled,
            'dof': details['dof'], 'type': details['type'],
            's_val': details['s_val'], 'e_val': details['e_val'], 'mid_val': details['mid_val']
        })

    status_line = {d: ["."] * max_time_scaled for d in DOFS}
    timeline = {d: [" "] * max_time_scaled for d in DOFS}

    for res in results:
        s, e = res['s'], res['e']
        dur = e - s
        dof = res['dof']
        if dur == 0: continue
        for t in range(s, e): status_line[dof][t] = "X"
        char_s = res['s_val'][0] if res['s_val'] else "?"
        char_e = res['e_val'][0] if res['e_val'] else "?"
        char_mid = res['mid_val'][0] if res['mid_val'] else "?"
        timeline[dof][s] = char_s
        if dur > 1:
            timeline[dof][e - 1] = char_e
            fill = char_mid if res['type'] in ['spin', 'hold'] else '#'
            for t in range(s + 1, e - 1): timeline[dof][t] = fill
        elif dur == 1:
            timeline[dof][s] = char_e

    print(f"\n[ASCII Optimized Gantt Chart] (Zoom: {zoom_factor}x)")
    print("Legend: | = Stage Separator, X = Busy, # = Moving")

    header_str = "   |"
    for t in range(max_time_scaled):
        if t in cut_points: header_str += "|"
        if t % zoom_factor == 0:
            real_t = t // zoom_factor
            header_str += str(real_t % 10)
        else:
            header_str += " "
    print(header_str + " |")

    for dof in DOFS:
        r1 = f"{dof:>2} |"
        for t in range(max_time_scaled):
            if t in cut_points: r1 += "|"
            r1 += status_line[dof][t]
        print(r1 + " |")
        r2 = "   |"
        for t in range(max_time_scaled):
            if t in cut_points: r2 += "|"
            r2 += timeline[dof][t]
        print(r2 + " |")


# ==========================================
# 4.5 JSON导出
# ==========================================

def export_json(solver, scheduler, total_makespan, n_pipes):
    """导出JSON格式数据"""
    tasks = []
    for name, (s_var, e_var, _) in scheduler.tasks.items():
        start = solver.Value(s_var)
        end = solver.Value(e_var)
        details = scheduler.task_details[name]
        tasks.append({
            "id": name,
            "name": name,
            "dof": details['dof'],
            "start": start,
            "end": end,
            "duration": end - start,
            "start_state": details['s_val'],
            "end_state": details['e_val'],
            "op_type": details['type']
        })

    # 按开始时间排序
    tasks.sort(key=lambda x: (x['start'], x['dof']))

    stage_cuts = [solver.Value(v) for v in scheduler.stage_cuts]

    return {
        "mode": "optimized",
        "n_pipes": n_pipes,
        "serial_time": scheduler.serial_duration,
        "optimized_time": total_makespan,
        "saved_time": scheduler.serial_duration - total_makespan,
        "stage_cuts": stage_cuts,
        "tasks": tasks
    }


# ==========================================
# 5. 主程序
# ==========================================

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Optimized drilling plan generator')
    parser.add_argument('--n_pipes', type=int, default=1, help='Number of drill pipes')
    parser.add_argument('--zoom', type=int, default=2, help='Zoom factor for ASCII output')
    parser.add_argument('--json', action='store_true', help='Output JSON format')
    parser.add_argument('--dur_config', type=str, default='', help='Path to custom durations JSON file')
    args = parser.parse_args()

    # 加载自定义时长配置
    if args.dur_config:
        load_durations_from_file(args.dur_config)

    N_PIPES = args.n_pipes
    ZOOM = args.zoom

    scheduler = RigScheduler()
    print(f"Building Full Cycle Schedule for N={N_PIPES}, Zoom={ZOOM}x...")

    # Stage A
    end_a = define_stage_A(scheduler)
    scheduler.stage_cuts.append(end_a)

    # Stage B Loop
    last_end = end_a
    for i in range(1, N_PIPES + 1):
        last_end = define_stage_B(scheduler, i, N_PIPES, last_end)
        scheduler.stage_cuts.append(last_end)

    # Stage C Loop
    for i in range(N_PIPES, 0, -1):
        last_end = define_stage_C(scheduler, i, N_PIPES, last_end)
        scheduler.stage_cuts.append(last_end)

    # Stage D (Final Tool Retrieval)
    end_d = define_stage_D(scheduler, last_end)
    scheduler.stage_cuts.append(end_d)

    # Solve
    solver_inst, total_time = scheduler.solve()

    if solver_inst:
        render_ascii_gantt(solver_inst, scheduler, total_time, zoom_factor=ZOOM)

        if args.json:
            json_data = export_json(solver_inst, scheduler, total_time, N_PIPES)
            print("\n[JSON OUTPUT]")
            print(json.dumps(json_data, ensure_ascii=False, indent=2))