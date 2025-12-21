# -*- coding: utf-8 -*-
from dataclasses import dataclass, asdict
from typing import List, Dict, Tuple
import argparse
import json
import sys
import os

DOFS = ["Fz", "Sr", "Me", "Mg", "Mr", "Dh", "Pr", "Cb"]

# ========= 1) Durations =========
# 默认时长配置
DEFAULT_DUR = {
    # A
    "A_FZ_AH": 8, "A_ME_to_store": 3, "A_MG_grip": 3, "A_ME_back": 3,
    "A_MR_to_head": 3, "A_ME_to_head": 3, "A_FZ_HG": 5,
    "A_COUPLE_GE": 6, "A_DH_lock": 1, "A_MG_release": 2,
    "A_ME_back_from_head": 3, "A_MR_back_to_store": 3,
    "A_DRILL": 10, "A_CB_clamp": 5, "A_DH_unlock": 1,
    "A_BREAK_AC": 6, "A_FZ_CH": 7,

    # B
    "SR_INDEX": 3,
    "B_ME_to_store": 3, "B_MG_grip": 3, "B_ME_back": 3,
    "B_MR_to_head": 3, "B_ME_to_head": 3,
    "B_FZ_HF": 4, "B_COUPLE_FD": 6,
    "B_DH_lock": 1, "B_MG_release": 2,
    "B_ME_back_from_head": 3, "B_MR_back_to_store": 3,
    "B_FZ_DJ": 4, "B_COUPLE_JI": 6,
    "B_CB_release": 5, "B_DRILL": 10, "B_CB_clamp": 5,
    "B_DH_unlock": 1, "B_BREAK_AC": 6, "B_FZ_CH": 7,

    # C — updated
    "C_FZ_HC": 6,
    "C_COUPLE_CB": 8,    # Fz C->B + Pr-B
    "C_DH_lock": 1,      # Dh A->B
    "C_CB_release": 5,   # Cb B->A
    "C_FZ_BI": 8,        # Fz B->I
    "C_CB_clamp": 5,     # Cb A->B
    "C_BREAK_IJ": 8,     # Fz I->J + Pr-C
    "C_FZ_JD": 6,        # Fz J->D
    "C_ASSIST_MR": 3, "C_ASSIST_ME": 3, "C_ASSIST_MG": 3,
    "C_DH_unlock": 1,
    "C_BREAK_DF": 8,     # Fz D->F + Pr-C
    "C_FZ_FH": 7,
    "C_SR_BACK": 3,      # Sr step back (after F->H)
    "C_REC_ME_CA": 3, "C_REC_MR_BA": 3, "C_REC_ME_AB": 3, "C_REC_MG_BA": 3, "C_REC_ME_BA": 3,

    # D — updated
    "D_FZ_HC": 6, "D_COUPLE_CB": 6, "D_DH_lock": 1,
    "D_CB_release": 5, "D_FZ_BE": 6,
    "D_MR_to_store": 3, "D_ME_to_head": 3, "D_MG_grip": 3,
    "D_DH_unlock": 1, "D_BREAK_EG": 6, "D_FZ_GH": 7,
    "D_SR_BACK_TO_A": 3,
    "D_REC_ME_CA": 3, "D_REC_MR_BA": 3, "D_REC_ME_AB": 3, "D_REC_MG_BA": 3, "D_REC_ME_BA": 3,
}

# 全局DUR变量，可被外部配置覆盖
DUR = DEFAULT_DUR.copy()

def load_durations_from_file(config_path: str):
    """从JSON文件加载时长配置"""
    global DUR
    if os.path.exists(config_path):
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                custom_dur = json.load(f)
        except Exception as exc:
            print(f"[WARN] Failed to load durations from {config_path}: {exc}")
            return

        if not isinstance(custom_dur, dict):
            print(f"[WARN] Durations config is not a JSON object: {config_path}")
            return

        for key, value in custom_dur.items():
            if key in DUR and isinstance(value, (int, float)):
                DUR[key] = int(value)
        print(f"[INFO] Loaded custom durations from: {config_path}")

# ========= 2) 原子操作 =========
@dataclass
class Op:
    dof: str
    kind: str               # 'move' | 'spin' | 'hold'
    start: str
    end: str
    dur: int
    state: str = ""         # for spin/hold

def pattern(op: Op) -> str:
    if op.dur <= 0:
        return ""
    if op.kind == "move":
        if op.dur == 1: return op.end
        return op.start + "#"*(op.dur-2) + op.end
    if op.kind == "spin":
        fill = op.state if op.state else op.end
        if op.dur == 1: return fill
        return op.start + fill*(op.dur-2) + op.end
    if op.kind == "hold":
        fill = op.state if op.state else op.start
        return fill * op.dur
    raise ValueError("unknown kind")

@dataclass
class Segment:
    name: str
    ops: List[Op]           # 同起同止

# ========= 3) Sr 盘位状态机 =========
class SrState:
    POS = list("ABCDEFG")   # 7 槽
    def __init__(self, start="A"): self.idx = self.POS.index(start)
    @property
    def letter(self) -> str: return self.POS[self.idx]
    def step_fwd(self): frm=self.letter; self.idx=(self.idx+1)%len(self.POS); return frm,self.letter
    def step_back(self): frm=self.letter; self.idx=(self.idx-1)%len(self.POS); return frm,self.letter

# ========= 4) 各阶段 =========
def stage_A() -> List[Segment]:
    S: List[Segment] = []
    S.append(Segment("A1 Fz A->H", [Op("Fz", "move", "A", "H", DUR["A_FZ_AH"])]))
    S.append(Segment("A2-1 Me A->B", [Op("Me", "move", "A", "B", DUR["A_ME_to_store"])]))
    S.append(Segment("A2-2 Mg A->B", [Op("Mg", "move", "A", "B", DUR["A_MG_grip"])]))
    S.append(Segment("A2-3 Me B->A", [Op("Me", "move", "B", "A", DUR["A_ME_back"])]))
    S.append(Segment("A3-1 Mr A->B", [Op("Mr", "move", "A", "B", DUR["A_MR_to_head"])]))
    S.append(Segment("A3-2 Me A->C", [Op("Me", "move", "A", "C", DUR["A_ME_to_head"])]))
    S.append(Segment("A4-1 Fz H->G", [Op("Fz", "move", "H", "G", DUR["A_FZ_HG"])]))
    S.append(Segment("A4-2 对接(G->E & Pr-B)", [
        Op("Fz", "move", "G", "E", DUR["A_COUPLE_GE"]),
        Op("Pr", "spin", "A", "A", DUR["A_COUPLE_GE"], state="B"),
    ]))
    S.append(Segment("A5 Dh 锁紧", [Op("Dh", "move", "A", "B", DUR["A_DH_lock"])]))
    S.append(Segment("A6 Mg B->A(松开)", [Op("Mg", "move", "B", "A", DUR["A_MG_release"])]))
    S.append(Segment("A7-1 Me C->A", [Op("Me", "move", "C", "A", DUR["A_ME_back_from_head"])]))
    S.append(Segment("A7-2 Mr B->A", [Op("Mr", "move", "B", "A", DUR["A_MR_back_to_store"])]))
    S.append(Segment("A8 钻具钻进", [
        Op("Fz", "move", "E", "A", DUR["A_DRILL"]),
        Op("Pr", "spin", "A", "A", DUR["A_DRILL"], state="D"),
    ]))
    S.append(Segment("A9 Cb A->B(夹紧)", [Op("Cb", "move", "A", "B", DUR["A_CB_clamp"])]))
    S.append(Segment("A10 Dh B->A(解锁)", [Op("Dh", "move", "B", "A", DUR["A_DH_unlock"])]))
    S.append(Segment("A11 断开(Fz A->C & Pr-C)", [
        Op("Fz", "move", "A", "C", DUR["A_BREAK_AC"]),
        Op("Pr", "spin", "A", "A", DUR["A_BREAK_AC"], state="C"),
    ]))
    S.append(Segment("A12 Fz C->H", [Op("Fz", "move", "C", "H", DUR["A_FZ_CH"])]))
    return S

def stage_B_one(i: int, sr: SrState) -> List[Segment]:
    S: List[Segment] = []
    tag = f"B{i}"
    frm, to = sr.step_fwd()
    S.append(Segment(f"{tag}-Sr {frm}->{to}", [Op("Sr", "move", frm, to, DUR["SR_INDEX"])]))
    S.append(Segment(f"{tag}-1 Me A->B", [Op("Me", "move", "A", "B", DUR["B_ME_to_store"])]))
    S.append(Segment(f"{tag}-2 Mg A->B", [Op("Mg", "move", "A", "B", DUR["B_MG_grip"])]))
    S.append(Segment(f"{tag}-3 Me B->A", [Op("Me", "move", "B", "A", DUR["B_ME_back"])]))
    S.append(Segment(f"{tag}-4 Mr A->B", [Op("Mr", "move", "A", "B", DUR["B_MR_to_head"])]))
    S.append(Segment(f"{tag}-5 Me A->C", [Op("Me", "move", "A", "C", DUR["B_ME_to_head"])]))
    S.append(Segment(f"{tag}-6 Fz H->F", [Op("Fz", "move", "H", "F", DUR["B_FZ_HF"])]))
    S.append(Segment(f"{tag}-7 对接1(Fz F->D & Pr-B)", [
        Op("Fz", "move", "F", "D", DUR["B_COUPLE_FD"]),
        Op("Pr", "spin", "A", "A", DUR["B_COUPLE_FD"], state="B"),
    ]))
    S.append(Segment(f"{tag}-8 Dh 锁紧", [Op("Dh", "move", "A", "B", DUR["B_DH_lock"])]))
    S.append(Segment(f"{tag}-9 Mg B->A(松开)", [Op("Mg", "move", "B", "A", DUR["B_MG_release"])]))
    S.append(Segment(f"{tag}-10 Me C->A", [Op("Me", "move", "C", "A", DUR["B_ME_back_from_head"])]))
    S.append(Segment(f"{tag}-11 Mr B->A", [Op("Mr", "move", "B", "A", DUR["B_MR_back_to_store"])]))
    S.append(Segment(f"{tag}-12 Fz D->J", [Op("Fz", "move", "D", "J", DUR["B_FZ_DJ"])]))
    S.append(Segment(f"{tag}-13 对接2(Fz J->I & Pr-B)", [
        Op("Fz", "move", "J", "I", DUR["B_COUPLE_JI"]),
        Op("Pr", "spin", "A", "A", DUR["B_COUPLE_JI"], state="B"),
    ]))
    S.append(Segment(f"{tag}-14 Cb B->A(松开)", [Op("Cb", "move", "B", "A", DUR["B_CB_release"])]))
    S.append(Segment(f"{tag}-15 延长钻进(Fz I->A & Pr-D)", [
        Op("Fz", "move", "I", "A", DUR["B_DRILL"]),
        Op("Pr", "spin", "A", "A", DUR["B_DRILL"], state="D"),
    ]))
    S.append(Segment(f"{tag}-16 Cb A->B(夹紧)", [Op("Cb", "move", "A", "B", DUR["B_CB_clamp"])]))
    S.append(Segment(f"{tag}-17 Dh B->A(解锁)", [Op("Dh", "move", "B", "A", DUR["B_DH_unlock"])]))
    S.append(Segment(f"{tag}-18 断开(Fz A->C & Pr-C)", [
        Op("Fz", "move", "A", "C", DUR["B_BREAK_AC"]),
        Op("Pr", "spin", "A", "A", DUR["B_BREAK_AC"], state="C"),
    ]))
    S.append(Segment(f"{tag}-19 Fz C->H", [Op("Fz", "move", "C", "H", DUR["B_FZ_CH"])]))
    return S

def stage_C_one(i_from_n: int, total_n: int, sr: SrState) -> List[Segment]:
    S: List[Segment] = []
    tag = f"C{total_n - i_from_n + 1}"

    S.append(Segment(f"{tag}-1 Fz H->C", [Op("Fz", "move", "H", "C", DUR["C_FZ_HC"])]))
    S.append(Segment(f"{tag}-2 上端对接(Fz C->B & Pr-B)", [
        Op("Fz", "move", "C", "B", DUR["C_COUPLE_CB"]),
        Op("Pr", "spin", "A", "A", DUR["C_COUPLE_CB"], state="B"),
    ]))
    S.append(Segment(f"{tag}-3 Dh A->B(锁紧)", [Op("Dh", "move", "A", "B", DUR["C_DH_lock"])]))
    S.append(Segment(f"{tag}-4 Cb B->A(松开)", [Op("Cb", "move", "B", "A", DUR["C_CB_release"])]))
    S.append(Segment(f"{tag}-5 Fz B->I", [Op("Fz", "move", "B", "I", DUR["C_FZ_BI"])]))
    S.append(Segment(f"{tag}-6 Cb A->B(夹紧)", [Op("Cb", "move", "A", "B", DUR["C_CB_clamp"])]))
    S.append(Segment(f"{tag}-7 断开(Fz I->J & Pr-C)", [
        Op("Fz", "move", "I", "J", DUR["C_BREAK_IJ"]),
        Op("Pr", "spin", "A", "A", DUR["C_BREAK_IJ"], state="C"),
    ]))
    S.append(Segment(f"{tag}-8 Fz J->D", [Op("Fz", "move", "J", "D", DUR["C_FZ_JD"])]))
    S.append(Segment(f"{tag}-9 Mr A->B(辅助)", [Op("Mr", "move", "A", "B", DUR["C_ASSIST_MR"])]))
    S.append(Segment(f"{tag}-10 Me A->C(辅助)", [Op("Me", "move", "A", "C", DUR["C_ASSIST_ME"])]))
    S.append(Segment(f"{tag}-11 Mg A->B(辅助)", [Op("Mg", "move", "A", "B", DUR["C_ASSIST_MG"])]))
    S.append(Segment(f"{tag}-12 Dh B->A(解锁)", [Op("Dh", "move", "B", "A", DUR["C_DH_unlock"])]))
    S.append(Segment(f"{tag}-13 断开中段(Fz D->F & Pr-C)", [
        Op("Fz", "move", "D", "F", DUR["C_BREAK_DF"]),
        Op("Pr", "spin", "A", "A", DUR["C_BREAK_DF"], state="C"),
    ]))
    S.append(Segment(f"{tag}-14 Fz F->H", [Op("Fz", "move", "F", "H", DUR["C_FZ_FH"])]))
    if i_from_n < total_n:
        frm, to = sr.step_back()
        S.append(Segment(f"{tag}-15 Sr {frm}->{to}", [Op("Sr", "move", frm, to, DUR["C_SR_BACK"])]))
    S.append(Segment(f"{tag}-16 Me C->A", [Op("Me", "move", "C", "A", DUR["C_REC_ME_CA"])]))
    S.append(Segment(f"{tag}-17 Mr B->A", [Op("Mr", "move", "B", "A", DUR["C_REC_MR_BA"])]))
    S.append(Segment(f"{tag}-18 Me A->B", [Op("Me", "move", "A", "B", DUR["C_REC_ME_AB"])]))
    S.append(Segment(f"{tag}-19 Mg B->A", [Op("Mg", "move", "B", "A", DUR["C_REC_MG_BA"])]))
    S.append(Segment(f"{tag}-20 Me B->A", [Op("Me", "move", "B", "A", DUR["C_REC_ME_BA"])]))
    return S

def stage_D(sr: SrState) -> List[Segment]:
    S: List[Segment] = []
    S.append(Segment("D1 Fz H->C", [Op("Fz", "move", "H", "C", DUR["D_FZ_HC"])]))
    S.append(Segment("D2 上端对接(Fz C->B & Pr-B)", [
        Op("Fz", "move", "C", "B", DUR["D_COUPLE_CB"]),
        Op("Pr", "spin", "A", "A", DUR["D_COUPLE_CB"], state="B"),
    ]))
    S.append(Segment("D3 Dh A->B(锁紧)", [Op("Dh", "move", "A", "B", DUR["D_DH_lock"])]))
    S.append(Segment("D4 Cb B->A(松开)", [Op("Cb", "move", "B", "A", DUR["D_CB_release"])]))
    S.append(Segment("D5 Fz B->E", [Op("Fz", "move", "B", "E", DUR["D_FZ_BE"])]))
    S.append(Segment("D6 Mr A->B", [Op("Mr", "move", "A", "B", DUR["D_MR_to_store"])]))
    S.append(Segment("D7 Me A->C", [Op("Me", "move", "A", "C", DUR["D_ME_to_head"])]))
    S.append(Segment("D8 Mg A->B", [Op("Mg", "move", "A", "B", DUR["D_MG_grip"])]))
    S.append(Segment("D9 Dh B->A(解锁)", [Op("Dh", "move", "B", "A", DUR["D_DH_unlock"])]))
    S.append(Segment("D10 断开(Pr-C & Fz E->G)", [
        Op("Pr", "spin", "A", "A", DUR["D_BREAK_EG"], state="C"),
        Op("Fz", "move", "E", "G", DUR["D_BREAK_EG"]),
    ]))
    S.append(Segment("D11 Fz G->H", [Op("Fz", "move", "G", "H", DUR["D_FZ_GH"])]))
    while sr.letter != "A":
        frm, to = sr.step_back()
        S.append(Segment(f"D-Sr {frm}->{to}", [Op("Sr", "move", frm, to, DUR["D_SR_BACK_TO_A"])]))
    S.append(Segment("D12 Me C->A", [Op("Me", "move", "C", "A", DUR["D_REC_ME_CA"])]))
    S.append(Segment("D13 Mr B->A", [Op("Mr", "move", "B", "A", DUR["D_REC_MR_BA"])]))
    S.append(Segment("D14 Me A->B", [Op("Me", "move", "A", "B", DUR["D_REC_ME_AB"])]))
    S.append(Segment("D15 Mg B->A", [Op("Mg", "move", "B", "A", DUR["D_REC_MG_BA"])]))
    S.append(Segment("D16 Me B->A", [Op("Me", "move", "B", "A", DUR["D_REC_ME_BA"])]))
    return S

# ========= 5) 串行拼接 + 渲染 =========
def build_timeline(segments: List[Segment]) -> Tuple[Dict[str, str], List[Tuple[int, int, str]]]:
    tl = {d: "" for d in DOFS}
    table: List[Tuple[int, int, str]] = []
    cursor = 0
    for seg in segments:
        width = max((op.dur for op in seg.ops), default=0)
        for d in DOFS:
            tl[d] += " " * width
        for op in seg.ops:
            patt = pattern(op)
            tl[op.dof] = tl[op.dof][:-width] + patt
        table.append((cursor, cursor + width, seg.name))
        cursor += width
    return tl, table

def insert_separators(s: str, cuts: List[int]) -> str:
    out = s
    for cut in sorted(cuts, reverse=True):
        out = out[:cut] + " | " + out[cut:]
    return out

def render_ascii(tl: Dict[str, str], table: List[Tuple[int, int, str]], big_cuts: List[int]) -> str:
    base_len = max(len(s) for s in tl.values())
    lines = []
    for d in DOFS:
        row = (tl[d] + " " * base_len)[:base_len]
        occ = ["X" if c != " " else "." for c in row]
        row_show = insert_separators(row, big_cuts)
        occ_show = insert_separators("".join(occ), big_cuts)
        lines.append(f"{d:>2} |{occ_show}|")
        lines.append(f"   |{row_show}|")
    scale = "".join(str(i % 10) for i in range(base_len))
    lines.append(f" t |{insert_separators(scale, big_cuts)}|")
    return "\n".join(lines)

def assemble_all(n_rods: int) -> Tuple[Dict[str, str], List[Tuple[int, int, str]], List[int]]:
    segs: List[Segment] = []
    sr = SrState(start="A")

    # A
    segs += stage_A()
    tl_tmp, tbl_tmp = build_timeline(segs); a_end = tbl_tmp[-1][1]

    # B
    for i in range(1, n_rods + 1):
        segs += stage_B_one(i, sr)
    tl_tmp, tbl_tmp = build_timeline(segs); b_end = tbl_tmp[-1][1]

    # C
    for i_from_n in range(n_rods, 0, -1):
        segs += stage_C_one(i_from_n, n_rods, sr)
    tl_tmp, tbl_tmp = build_timeline(segs); c_end = tbl_tmp[-1][1]

    # D
    segs += stage_D(sr)
    tl, table = build_timeline(segs)

    big_cuts = [a_end, b_end, c_end]   # A|B|C|D
    return tl, table, big_cuts

def export_json(tl: Dict[str, str], table: List[Tuple[int, int, str]], big_cuts: List[int], n_rods: int) -> dict:
    """导出JSON格式数据"""
    tasks = []
    for i, (s, e, name) in enumerate(table):
        # 解析任务名称提取dof信息
        dof = None
        for d in DOFS:
            if d in name or d.lower() in name.lower():
                dof = d
                break
        tasks.append({
            "id": i,
            "name": name,
            "start": s,
            "end": e,
            "duration": e - s,
            "dof": dof
        })

    total_time = table[-1][1] if table else 0

    return {
        "mode": "serial",
        "n_pipes": n_rods,
        "total_time": total_time,
        "stage_cuts": big_cuts,
        "tasks": tasks,
        "dof_timelines": {d: tl[d] for d in DOFS}
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Serial drilling plan generator')
    parser.add_argument('--n_pipes', type=int, default=1, help='Number of drill pipes')
    parser.add_argument('--zoom', type=int, default=1, help='Zoom factor (unused in serial)')
    parser.add_argument('--json', action='store_true', help='Output JSON format')
    parser.add_argument('--dur_config', type=str, default='', help='Path to custom durations JSON file')
    args = parser.parse_args()

    # 加载自定义时长配置
    if args.dur_config:
        load_durations_from_file(args.dur_config)

    N = args.n_pipes
    tl, table, big_cuts = assemble_all(N)
    print(f"=== 严格串行（serial）— n={N} ===\n")
    for i, (s, e, name) in enumerate(table, 1):
        print(f"{i:>3}. [{s:>5},{e:>5}) dur={e-s:>3}  {name}")
    print("\n[ASCII 甘特（含阶段分隔 ' | '）]\n")
    print(render_ascii(tl, table, big_cuts))

    if args.json:
        json_data = export_json(tl, table, big_cuts, N)
        print("\n[JSON OUTPUT]")
        print(json.dumps(json_data, ensure_ascii=False, indent=2))
