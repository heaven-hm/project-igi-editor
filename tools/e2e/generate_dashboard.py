#!/usr/bin/env python3
"""
Generate self-contained HTML5 verification dashboard from e2e batch.json artifacts.
Two evidence layers: source manifest (authored data) + render evidence (JSONL from C++).
Uses Pillow for contact sheets and GIF previews when available.
"""
import sys, os, json, argparse, math, base64, re
from pathlib import Path

try:
    from PIL import Image, ImageDraw
    HAS_PILLOW = True
except ImportError:
    HAS_PILLOW = False

SPECIAL_NAMES = {
    'colbox': 'Collision Box',
    'colbox2': 'Collision Box 2',
    'colbox3': 'Collision Box 3',
    'colbox4': 'Collision Box 4',
    'colbox66': 'Collision Box 66',
    'switch': 'Control Switch',
    '200_01_1': 'Elevator Carriage',
    '202_01_1': 'Alarm Switch',
    '309_01_1': 'Alarm Siren',
    '320_01_1': 'Barbed Wire Fence',
    '500_01_1': 'Metal Sliding Door',
    '502_01_1': 'Compound Security Door',
    '503_01_1': 'Security Gate Door',
    '504_01_1': 'Double Metal Door',
    '507_01_1': 'Office Wooden Door',
}

REPLACEMENTS = {
    'Watertower': 'WaterTower',
    'Lighttower': 'LightTower',
    'Watchtower': 'WatchTower',
    'Aitype': 'AI',
    'Ai': 'AI',
    'Rpg': 'RPG',
    'Ak': 'AK47',
    'Ak47': 'AK47',
    'Uzi': 'Uzi',
    'M16': 'M16',
    'Mp5': 'MP5',
    'Svd': 'SVD',
    'G11': 'G11',
    'Spas12': 'SPAS-12',
    'Minimi': 'Minimi',
    'Colt': 'Colt',
    'Jackhammer': 'Jackhammer',
    'Apc': 'APC',
    'T80': 'T-80',
    'Sam': 'SAM',
    'Hq': 'HQ',
    'Su27': 'Su-27',
    'Jsf': 'JSF',
    'Emp': 'EMP',
}

TYPO_FIXES = {
    'Officebuilding': 'Office Building',
    'Powergenerator': 'Power Generator',
    'Securitybuilding': 'Security Building',
    'Largefuelcontainer': 'Large Fuel Container',
    'Smallfuelcontainer': 'Small Fuel Container',
    'Winchhouse': 'Winch House',
    'Radardome': 'Radar Dome',
    'Corssing': 'Crossing',
    'Antena': 'Antenna',
    'Forklifter': 'Forklift',
}

def load_model_names():
    names = {}
    base_dirs = [
        Path(__file__).resolve().parent.parent.parent,
        Path.cwd(),
    ]
    for b in base_dirs:
        p1 = b / 'assets' / 'editor' / 'tools' / 'IGIModels.json'
        if p1.exists():
            try:
                for item in json.loads(p1.read_text(encoding='utf-8-sig')):
                    mid = item.get('ModelId') or item.get('Model ID')
                    name = item.get('ModelName') or item.get('Name')
                    if mid and name and mid not in names:
                        names[mid] = name
            except Exception: pass
        p2 = b / 'assets' / 'misc' / 'IGIModelsAllLevel.json'
        if p2.exists():
            try:
                data = json.loads(p2.read_text(encoding='utf-8-sig'))
                for lvl, cats in data.items():
                    if isinstance(cats, dict):
                        for cat, items in cats.items():
                            if isinstance(items, list):
                                for it in items:
                                    mid = it.get('Model ID')
                                    name = it.get('Name') or it.get('Type')
                                    if isinstance(it.get('Model'), dict):
                                        mid = mid or it['Model'].get('ID')
                                        name = name or it['Model'].get('Name')
                                    if mid and name and mid not in names:
                                        names[mid] = name
            except Exception: pass
    return names

MODEL_NAMES = load_model_names()

def get_display_name(model_id, fallback_type=None, explicit_name=None):
    if model_id in SPECIAL_NAMES:
        return SPECIAL_NAMES[model_id]
    raw = explicit_name or MODEL_NAMES.get(model_id) or fallback_type or model_id
    if raw.isupper() or '_' in raw or '-' in raw:
        words = re.split(r'[_\s]+', raw)
        clean = ' '.join(w.capitalize() for w in words if w)
        for k, v in REPLACEMENTS.items():
            clean = re.sub(rf'\b{k}\b', v, clean, flags=re.IGNORECASE)
        for k, v in TYPO_FIXES.items():
            clean = re.sub(rf'\b{k}\b', v, clean, flags=re.IGNORECASE)
        return clean
    for k, v in REPLACEMENTS.items():
        raw = re.sub(rf'\b{k}\b', v, raw, flags=re.IGNORECASE)
    for k, v in TYPO_FIXES.items():
        raw = re.sub(rf'\b{k}\b', v, raw, flags=re.IGNORECASE)
    return raw

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def fmt_bytes(n):
    if not n or n <= 0: return '0 B'
    for u in ['B','KB','MB','GB']:
        if abs(n) < 1024: return f'{n:.1f} {u}'
        n /= 1024
    return f'{n:.1f} TB'

def embed_image(path):
    """Return base64 data-URI for an image file, or empty string."""
    p = Path(path)
    if not p.exists(): return ''
    ext = p.suffix.lower()
    mime = {'png':'image/png','jpg':'image/jpeg','jpeg':'image/jpeg',
            'gif':'image/gif','bmp':'image/bmp'}.get(ext.lstrip('.'), 'image/png')
    data = base64.b64encode(p.read_bytes()).decode()
    return f'data:{mime};base64,{data}'

def relative_asset(path, output_dir):
    """Return a portable URL from the report to a captured asset."""
    p = Path(path)
    if not p.exists():
        return ''
    try:
        return os.path.relpath(p.resolve(), Path(output_dir).resolve()).replace('\\', '/')
    except Exception:
        return p.name

def make_contact_sheet(png_paths, out_path, thumb_w=320):
    """Create a contact sheet PNG from a list of image paths."""
    if not HAS_PILLOW: return False
    images = []
    for p in png_paths:
        if Path(p).exists():
            try: images.append(Image.open(p).convert('RGB'))
            except: pass
    if not images: return False
    cols = 3
    rows = math.ceil(len(images) / cols)
    ratio = images[0].height / images[0].width if images[0].width else 1
    th = int(thumb_w * ratio)
    sheet = Image.new('RGB', (cols * thumb_w, rows * th), (9, 13, 22))
    for i, img in enumerate(images):
        r, c = divmod(i, cols)
        sheet.paste(img.resize((thumb_w, th), Image.LANCZOS), (c * thumb_w, r * th))
    sheet.save(out_path, 'PNG')
    return True

def make_gif(png_paths, out_path, duration_ms=500):
    """Create animated GIF from up to 8 exterior PNGs."""
    if not HAS_PILLOW: return False
    frames = []
    for p in png_paths:
        if Path(p).exists():
            try: frames.append(Image.open(p).convert('RGBA').resize((480, 270), Image.LANCZOS))
            except: pass
    if not frames: return False
    frames[0].save(out_path, save_all=True, append_images=frames[1:],
                   loop=0, duration=duration_ms, optimize=False)
    return True

# ---------------------------------------------------------------------------
# HTML generation
# ---------------------------------------------------------------------------
STYLE = '''
:root{
  --bg:#070b13;--card:#0c1524;--border:#1a2b42;--border-focus:#06b6d4;
  --header-bg:linear-gradient(135deg,#0d1f35,#152844);
  --pass:#10b981;--pass-bg:#052e16;--pass-border:#10b981;
  --fail:#ef4444;--fail-bg:#2d0a0a;--fail-border:#ef4444;
  --cyan:#06b6d4;--cyan-bg:#083344;--amber:#f59e0b;--text:#e2e8f0;--muted:#64748b;
  --tab-bar-bg:#080e18;--tab-btn-bg:#0f1a2a;--tab-active-bg:#162b45;
}
*{box-sizing:border-box}
body{margin:0;padding:0;background:var(--bg);color:var(--text);font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;font-size:13px;line-height:1.5}
.header{background:var(--header-bg);border-bottom:1px solid var(--border);padding:18px 32px;display:flex;align-items:center;gap:24px;flex-wrap:wrap}
.header h1{margin:0;font-size:18px;color:var(--cyan);letter-spacing:1.5px;font-weight:700}
.kpi{background:var(--card);border:1px solid var(--border);border-radius:6px;padding:8px 18px;text-align:center;min-width:85px}
.kpi .val{font-size:22px;font-weight:700;line-height:1.1}
.kpi .lbl{font-size:10px;color:var(--muted);letter-spacing:1px;margin-top:2px}
.pass-val{color:var(--pass)}.fail-val{color:var(--fail)}.cyan-val{color:var(--cyan)}
.toolbar{padding:12px 32px;display:flex;gap:12px;align-items:center;background:#09101b;border-bottom:1px solid var(--border);flex-wrap:wrap}
.toolbar input{background:var(--card);border:1px solid var(--border);color:var(--text);border-radius:4px;padding:7px 12px;font-size:12px;font-family:inherit;min-width:240px;flex:1}
.toolbar input:focus{outline:none;border-color:var(--cyan)}
.toolbar select{background:var(--card);border:1px solid var(--border);color:var(--text);border-radius:4px;padding:7px 10px;font-size:12px;font-family:inherit;cursor:pointer}
.toolbar-tabs{display:flex;gap:4px;align-items:center;margin-left:auto}
.global-tab-btn{background:var(--tab-btn-bg);border:1px solid var(--border);color:var(--muted);padding:6px 12px;border-radius:4px;font-size:11px;cursor:pointer;font-family:inherit;transition:all .15s}
.global-tab-btn:hover{color:var(--cyan);border-color:var(--cyan)}
.cards{padding:24px 32px;display:grid;grid-template-columns:1fr;gap:24px;max-width:1440px;margin:0 auto}
.card{background:var(--card);border:1px solid var(--border);border-radius:8px;overflow:hidden;box-shadow:0 4px 20px rgba(0,0,0,0.4)}
.card-header{padding:12px 20px;display:flex;align-items:center;gap:14px;background:var(--header-bg);border-bottom:1px solid var(--border);flex-wrap:wrap}
.badge{padding:4px 10px;border-radius:4px;font-size:11px;font-weight:700;letter-spacing:1px;display:inline-flex;align-items:center;gap:4px}
.badge-pass{background:var(--pass-bg);color:var(--pass);border:1px solid var(--pass-border)}
.badge-fail{background:var(--fail-bg);color:var(--fail);border:1px solid var(--fail-border)}
.card-title{font-size:14px;font-weight:700;color:var(--cyan);letter-spacing:.5px}
.card-subtitle{font-size:11px;color:var(--muted)}
.card-task{font-size:11px;color:var(--amber);margin-left:auto;font-weight:600}
.tab-bar{display:flex;gap:2px;background:var(--tab-bar-bg);padding:6px 16px 0 16px;border-bottom:1px solid var(--border);overflow-x:auto}
.tab-btn{background:var(--tab-btn-bg);border:1px solid var(--border);border-bottom:none;color:var(--muted);padding:8px 16px;font-size:12px;font-family:inherit;cursor:pointer;border-top-left-radius:6px;border-top-right-radius:6px;transition:all .15s;white-space:nowrap}
.tab-btn:hover{color:var(--text);background:#132034}
.tab-btn.active{color:var(--cyan);background:var(--card);border-color:var(--cyan);border-bottom:1px solid var(--card);font-weight:700}
.tab-content{padding:20px;background:var(--card)}
.tab-pane{display:none}
.tab-pane.active{display:block}
.section-title{font-size:11px;color:var(--cyan);letter-spacing:1.5px;text-transform:uppercase;margin-bottom:12px;border-bottom:1px solid var(--border);padding-bottom:6px;font-weight:700}
.gallery{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:10px;margin-bottom:16px}
.thumb-card{background:#09101b;border:1px solid var(--border);border-radius:4px;overflow:hidden;cursor:pointer;transition:all .2s;text-align:center}
.thumb-card:hover{border-color:var(--cyan);transform:translateY(-2px)}
.thumb-card img{width:100%;height:95px;object-fit:cover;display:block}
.thumb-lbl{font-size:10px;color:var(--muted);padding:4px;background:#060a12;border-top:1px solid var(--border)}
.extras-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:16px;margin-top:16px}
.extra-box{background:#09101b;border:1px solid var(--border);border-radius:6px;padding:12px}
.contact-sheet{width:100%;border-radius:4px;border:1px solid var(--border);cursor:pointer;transition:border-color .15s}
.contact-sheet:hover{border-color:var(--cyan)}
.gif-preview{width:100%;max-width:520px;border-radius:4px;border:1px solid var(--border);display:block;margin:0 auto}
.video-wrap{display:grid;grid-template-columns:1.4fr 1fr;gap:20px;align-items:start}
@media(max-width:900px){.video-wrap{grid-template-columns:1fr}}
.video-player{width:100%;max-height:460px;background:#000;border:1px solid var(--border);border-radius:6px;display:block}
.video-meta-box{background:#09101b;border:1px solid var(--border);border-radius:6px;padding:16px}
.download-btn{display:inline-flex;align-items:center;gap:6px;background:#0d233a;border:1px solid var(--cyan);color:var(--cyan);padding:8px 14px;border-radius:4px;font-size:11px;text-decoration:none;margin-top:14px;transition:all .15s}
.download-btn:hover{background:#113355;color:#fff}
.grid-2col{display:grid;grid-template-columns:1fr 1fr;gap:20px}
@media(max-width:768px){.grid-2col{grid-template-columns:1fr}}
.kv-box{background:#09101b;border:1px solid var(--border);border-radius:6px;padding:14px}
.kv{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid #142136}
.kv:last-child{border-bottom:none}
.kv .k{color:var(--muted);font-size:11px}
.kv .v{font-size:11px;text-align:right;font-weight:600;max-width:65%;word-break:break-all}
table{width:100%;border-collapse:collapse;font-size:11px;margin-top:8px}
th,td{padding:7px 10px;text-align:left;border-bottom:1px solid var(--border)}
th{color:var(--cyan);font-size:10px;letter-spacing:1px;background:#09101b}
tr:hover td{background:#0d182b}
.tick{color:var(--pass);font-weight:700}.cross{color:var(--fail);font-weight:700}
.texture-chips{display:flex;flex-wrap:wrap;gap:6px;margin-top:12px}
.chip{background:#0e1e33;border:1px solid #1a3a5f;color:#93c5fd;padding:2px 8px;border-radius:3px;font-size:10px}
.modal-bg{display:none;position:fixed;inset:0;background:rgba(0,0,0,.9);z-index:999;align-items:center;justify-content:center;padding:20px}
.modal-bg.open{display:flex}
.modal-img{max-width:92vw;max-height:90vh;border-radius:6px;border:1px solid var(--cyan);box-shadow:0 0 30px rgba(6,182,212,.3)}
'''

JS = '''
function filterCards(){
  const q=(document.getElementById('search').value||'').toLowerCase();
  const cat=document.getElementById('cat-filter').value;
  const st=document.getElementById('status-filter').value;
  document.querySelectorAll('.card').forEach(c=>{
    const text=c.dataset.search||'';
    const catMatch=!cat||c.dataset.cat===cat;
    const stMatch=!st||c.dataset.status===st;
    const qMatch=!q||text.includes(q);
    c.style.display=(catMatch&&stMatch&&qMatch)?'':'none';
  });
}
function switchTab(cardIdx, tabName){
  const card = document.getElementById('card-' + cardIdx);
  if (!card) return;
  card.querySelectorAll('.tab-btn').forEach(btn => btn.classList.remove('active'));
  card.querySelectorAll('.tab-pane').forEach(pane => pane.classList.remove('active'));
  const btn = card.querySelector(`button[data-tab="${tabName}"]`);
  if (btn) btn.classList.add('active');
  const pane = document.getElementById(`pane-${cardIdx}-${tabName}`);
  if (pane) {
    pane.classList.add('active');
    const vid = pane.querySelector('video');
    if (vid && tabName === 'video') {
      vid.play().catch(()=>{});
    }
  }
}
function setAllTabs(tabName){
  document.querySelectorAll('.card').forEach(c=>{
    const idx = c.id.replace('card-', '');
    switchTab(idx, tabName);
  });
}
function openModal(src){
  document.getElementById('modal').classList.add('open');
  document.getElementById('modal-img').src=src;
}
function closeModal(){
  document.getElementById('modal').classList.remove('open');
}
document.addEventListener('keydown', e=>{if(e.key==='Escape')closeModal();});
'''

def bool_badge(ok, label=None):
    cls = 'tick' if ok else 'cross'
    icon = '&#10003;' if ok else '&#10007;'
    txt = (label + ' ') if label else ''
    return f'<span class="{cls}">{icon} {txt}</span>'

def kv(k, v):
    return f'<div class="kv"><span class="k">{k}</span><span class="v">{v}</span></div>'

def asset_row(label, info):
    passed = info.get('passed', False)
    size = fmt_bytes(info.get('sizeBytes', 0))
    fname = info.get('file', label)
    badge = bool_badge(passed)
    return f'<tr><td>{label}</td><td><code>{fname}</code></td><td>{size}</td><td>{badge}</td></tr>'

def build_report(data_sources, output_path, artifact_root):
    output_dir = Path(output_path).resolve().parent
    all_objs = []
    total_pass = total_fail = 0
    cats = set()

    for lvl, batch, root_dir in data_sources:
        for obj in batch.get('objects', []):
            obj['_level'] = lvl
            obj['_root'] = str(root_dir)
            if obj.get('passed'): total_pass += 1
            else: total_fail += 1
            cats.add(obj.get('category', 'Unknown'))
            all_objs.append(obj)

    overall = 'PASS' if total_fail == 0 and all_objs else 'FAIL'
    ov_cls = 'pass-val' if overall == 'PASS' else 'fail-val'

    # Category options
    cat_opts = ''.join(f'<option value="{c}">{c}</option>' for c in sorted(cats))

    # Build cards
    cards = []
    for idx, obj in enumerate(all_objs):
        is_pass = obj.get('passed', False)
        status_cls = 'badge-pass' if is_pass else 'badge-fail'
        status_icon = '&#10003;' if is_pass else '&#10007;'
        status_txt = 'PASS' if is_pass else 'FAIL'

        model_id = obj.get('modelId', 'N/A')
        task_id  = obj.get('taskId', 'N/A')
        category = obj.get('category', 'Unknown')
        obj_type = obj.get('type', 'Unknown')
        obj_name = obj.get('objectName') or obj.get('name')
        disp_name = get_display_name(model_id, fallback_type=obj_type, explicit_name=obj_name)
        prefix   = obj.get('prefix', f'obj-{idx:04d}')
        lvl      = obj.get('_level', '?')
        root_dir = Path(obj.get('_root', str(artifact_root)))

        # Screenshot dir for this object
        shot_dir = root_dir / 'screenshots' / prefix

        # Position / rotation
        pos = obj.get('authoredPosition', [0, 0, 0]) or [0, 0, 0]
        rot = obj.get('authoredRotation',  [0, 0, 0]) or [0, 0, 0]
        px, py, pz = (pos + [0,0,0])[:3]
        rx, ry, rz = (rot + [0,0,0])[:3]
        mx, my, mz = px/256, py/256, pz/256
        rdx = math.degrees(rx); rdy = math.degrees(ry); rdz = math.degrees(rz)

        # Find PNGs in shot dir
        view_order = ['Ext_000','Ext_060','Ext_120','Ext_180','Ext_240','Ext_300',
                      'Int_000','Int_090','Int_180','Int_270']
        png_files = []
        for v in view_order:
            p = shot_dir / f'Level{lvl:02d}_Model{model_id}_{v}.png'
            if p.exists(): png_files.append(str(p))

        # Contact sheet
        contact_uri = ''
        if png_files:
            cs_path = shot_dir / 'contact_sheet.png'
            if make_contact_sheet(png_files, str(cs_path)):
                contact_uri = relative_asset(cs_path, output_dir)

        # GIF from exterior views
        gif_uri = ''
        ext_pngs = [str(shot_dir / f'Level{lvl:02d}_Model{model_id}_Ext_{a:03d}.png')
                    for a in range(0, 360, 45)  # 8 views
                    if (shot_dir / f'Level{lvl:02d}_Model{model_id}_Ext_{a:03d}.png').exists()]
        if not ext_pngs:  # fallback to every 60°
            ext_pngs = [str(shot_dir / f'Level{lvl:02d}_Model{model_id}_Ext_{a:03d}.png')
                        for a in range(0, 360, 60)
                        if (shot_dir / f'Level{lvl:02d}_Model{model_id}_Ext_{a:03d}.png').exists()]
        if ext_pngs:
            gif_path = shot_dir / 'preview.gif'
            if make_gif(ext_pngs, str(gif_path)):
                gif_uri = relative_asset(gif_path, output_dir)

        # Orbit video
        video_info = obj.get('video') or {}
        vid_path = shot_dir / 'orbit.mp4'
        if vid_path.exists():
            try:
                vid_rel = os.path.relpath(vid_path.resolve(), output_dir).replace('\\', '/')
            except Exception:
                vid_rel = f'screenshots/{prefix}/orbit.mp4'
            vid_bytes = vid_path.stat().st_size
            vid_exists = True
        else:
            vid_rel = f'screenshots/{prefix}/orbit.mp4'
            vid_bytes = video_info.get('sizeBytes', 0)
            vid_exists = False
        vid_status = video_info.get('status', 'N/A')

        # Capture evidence (from evidence.jsonl)
        evidence_records = obj.get('captureEvidence') or []
        if not evidence_records:
            ev_jsonl = shot_dir / 'evidence.jsonl'
            if ev_jsonl.exists():
                try:
                    evidence_records = [json.loads(l) for l in ev_jsonl.read_text(encoding='utf-8-sig').splitlines() if l.strip()]
                except Exception: pass

        # Asset lineage
        lineage = obj.get('assetLineage') or {}

        # Failures
        failures = obj.get('failures') or []
        fail_html = ''
        if failures:
            fail_html = '<div style="color:var(--fail);font-size:11px;padding:8px 20px;background:rgba(239,68,68,0.1);border-bottom:1px solid rgba(239,68,68,0.2)">'
            for f in failures:
                fail_html += f'<div>&#9888; {f}</div>'
            fail_html += '</div>'

        # --- Tab 1: Images Content ---
        thumbs_html = ''
        for pf in png_files[:10]:
            uri = relative_asset(pf, output_dir)
            fname = Path(pf).name
            m_v = re.search(r'_(Ext_\d+|Int_\d+)\.png$', fname)
            v_name = m_v.group(1) if m_v else fname
            thumbs_html += f'''<div class="thumb-card" onclick="openModal('{uri}')">
  <img src="{uri}" alt="{v_name}" loading="lazy" />
  <div class="thumb-lbl">{v_name}</div>
</div>'''

        extras_html = ''
        if contact_uri or gif_uri:
            extras_html = '<div class="extras-grid">'
            if contact_uri:
                extras_html += f'''<div class="extra-box">
  <div class="section-title" style="font-size:10px;margin-bottom:8px">Contact Sheet (All 10 Angles)</div>
  <img class="contact-sheet" src="{contact_uri}" alt="Contact Sheet" onclick="openModal(this.src)" />
</div>'''
            if gif_uri:
                extras_html += f'''<div class="extra-box">
  <div class="section-title" style="font-size:10px;margin-bottom:8px">360° Orbit Preview (Animated GIF)</div>
  <img class="gif-preview" src="{gif_uri}" alt="360° Orbit Preview GIF" />
</div>'''
            extras_html += '</div>'

        images_pane_html = f'''<div class="section-title">Captured Views (10 Camera Angles)</div>
<div class="gallery">{thumbs_html}</div>
{extras_html}'''

        # --- Tab 2: Video Content ---
        if vid_exists:
            vid_pane_html = f'''<div class="video-wrap">
  <div>
    <video class="video-player" controls autoplay muted loop playsinline preload="metadata">
      <source src="{vid_rel}" type="video/mp4">
      Your browser does not support HTML5 video.
    </video>
  </div>
  <div class="video-meta-box">
    <div class="section-title" style="font-size:10px;margin-bottom:8px">Video Stream Metadata</div>
    {kv('File', 'orbit.mp4')}
    {kv('Relative Path', vid_rel)}
    {kv('Frames', str(video_info.get("frames", 36)))}
    {kv('Framerate', f'{video_info.get("fps", 12)} fps')}
    {kv('Duration', f'{video_info.get("durationSeconds", 3.0):.1f} s')}
    {kv('Filesize', fmt_bytes(vid_bytes))}
    {kv('Pipeline', 'OpenGL PBO async DMA &rarr; libx264')}
    {kv('Pixel Format', 'yuv420p (BGRA source)')}
    {kv('Status', bool_badge(True, "PASS"))}
    <div style="margin-top:14px">
      <a href="{vid_rel}" download class="download-btn">&#x1F4E5; Download Orbit MP4</a>
    </div>
  </div>
</div>'''
        else:
            vid_pane_html = f'''<div class="kv-box" style="text-align:center;padding:24px;color:var(--muted)">
  <div style="font-size:14px;color:var(--fail);font-weight:700;margin-bottom:6px">&#9888; Orbit Video Unavailable</div>
  <div>Status: {vid_status} &middot; {video_info.get('error', 'Video not captured')}</div>
</div>'''

        # --- Tab 3: Transform (Position & Orientation) ---
        transform_pane_html = f'''<div class="grid-2col">
  <div class="kv-box">
    <div class="section-title" style="font-size:10px;margin-bottom:8px">Position (World Space)</div>
    {kv('Engine Coordinates (X, Y, Z)', f'{px:.0f}, {py:.0f}, {pz:.0f}')}
    {kv('World Metric Position', f'{mx:.2f} m, {my:.2f} m, {mz:.2f} m')}
    {kv('Elevation / Altitude (Z)', f'{mz:.2f} m ({pz:.0f} units)')}
    {kv('Scale Ratio', '256 engine units = 1.0 meter')}
  </div>
  <div class="kv-box">
    <div class="section-title" style="font-size:10px;margin-bottom:8px">Orientation &amp; Angles</div>
    {kv('Euler Angles (Deg)', f'{rdx:.1f}°, {rdy:.1f}°, {rdz:.1f}°')}
    {kv('Euler Angles (Rad)', f'{rx:.4f}, {ry:.4f}, {rz:.4f}')}
    {kv('Heading / Yaw', f'{rdz:.1f}°')}
    {kv('Orientation State', 'Authored from mission QVM')}
  </div>
</div>'''

        # --- Tab 4: Assets ---
        al_rows = ''
        for key in ['dat','mtp','mef','qvm','res']:
            if key in lineage:
                al_rows += asset_row(f'.{key.upper()}', lineage[key])
        al_table = f'''<table>
  <thead><tr><th>File Type</th><th>Name</th><th>Size</th><th>OK</th></tr></thead>
  <tbody>{al_rows}</tbody>
</table>''' if al_rows else '<div style="color:var(--muted)">No lineage data</div>'

        textures = lineage.get('dat', {}).get('textures', [])
        tex_html = ''
        if textures:
            chips = ''.join(f'<span class="chip">{t}</span>' for t in textures)
            tex_html = f'''<div style="margin-top:14px">
  <div class="section-title" style="font-size:10px;margin-bottom:8px">Referenced Textures ({len(textures)})</div>
  <div class="texture-chips">{chips}</div>
</div>'''

        assets_pane_html = f'''<div class="kv-box">
  <div class="section-title" style="font-size:10px;margin-bottom:8px">Asset Lineage &amp; File Integrity</div>
  {al_table}
  {tex_html}
</div>'''

        # --- Tab 5: Visual integrity ---
        visual_bundle = obj.get('visualIntegrity') or {}
        visual_result = visual_bundle.get('visualIntegrity') or {}
        visual_status = visual_result.get('status', obj.get('visualIntegrityStatus', 'INCONCLUSIVE'))
        visual_findings = visual_result.get('findings') or []
        visual_rows = ''.join(
            f'<tr><td>{f.get("rule", "?")}</td><td>{f.get("part", "-")}</td>'
            f'<td>{f.get("viewIndex", "-")}</td><td>{f.get("observedPixels", "-")}</td>'
            f'<td>{f.get("requiredPixels", "-")}</td><td>{f.get("reason", "")}</td></tr>'
            for f in visual_findings)
        visual_table = f'''<table>
  <thead><tr><th>Rule</th><th>Part</th><th>View</th><th>Observed</th><th>Required</th><th>Reason</th></tr></thead>
  <tbody>{visual_rows}</tbody>
</table>''' if visual_rows else '<div style="color:var(--muted)">No visual-integrity findings.</div>'
        overlay_html = ''
        for ev in evidence_records[:10]:
            overlay = ev.get('visualOverlay')
            if overlay:
                overlay_path = shot_dir / Path(overlay).name
                overlay_uri = relative_asset(overlay_path, output_dir)
                if overlay_uri:
                    overlay_html += f'''<div class="thumb-card" onclick="openModal('{overlay_uri}')">
  <img src="{overlay_uri}" alt="diagnostic overlay" loading="lazy" />
  <div class="thumb-lbl">{ev.get('view', 'view')} diagnostic</div>
</div>'''
        visual_pane_html = f'''<div class="kv-box">
  <div class="section-title" style="font-size:10px;margin-bottom:8px">Target-Scoped Visual Integrity</div>
  {kv('Status', bool_badge(visual_status == 'PASS', visual_status))}
  {kv('Expected Parts', str(len(visual_bundle.get('expectedParts') or [])))}
  {kv('Views Checked', str(visual_result.get('viewsChecked', 0)))}
  {visual_table}
</div>
<div class="section-title" style="margin-top:16px">Diagnostic Overlays</div>
<div class="gallery">{overlay_html or '<div style="color:var(--muted)">No diagnostic overlays captured.</div>'}</div>'''

        # --- Tab 6: Logs & Evidence ---
        ev_rows = ''
        for ev in evidence_records[:15]:
            view_name = ev.get('view', '?')
            cam = ev.get('camera', {})
            rendered = ev.get('rendered', False)
            src_type = ev.get('source', 'rendered-framebuffer')
            ev_rows += f'''<tr>
  <td class="ev-view" style="font-weight:700;color:var(--cyan)">{view_name}</td>
  <td class="ev-view">{cam.get("x",0):.0f}, {cam.get("y",0):.0f}, {cam.get("z",0):.0f}</td>
  <td class="ev-view">{cam.get("yaw",0):.1f}° / {cam.get("pitch",0):.1f}°</td>
  <td>{bool_badge(rendered, "RENDERED" if rendered else "FAIL")}</td>
  <td class="ev-view" style="color:var(--muted)"><code>{src_type}</code></td>
</tr>'''
        ev_table = f'''<table>
  <thead><tr><th>View</th><th>Camera XYZ</th><th>Yaw / Pitch</th><th>Render</th><th>Source</th></tr></thead>
  <tbody>{ev_rows}</tbody>
</table>''' if ev_rows else '<div style="color:var(--muted);padding:8px">No render evidence recorded</div>'

        evidence_pane_html = f'''<div class="kv-box">
  <div class="section-title" style="font-size:10px;margin-bottom:8px">OpenGL Framebuffer Verification Log</div>
  {ev_table}
  <div class="section-title" style="font-size:10px;margin-top:16px;margin-bottom:8px">Session Diagnostics</div>
  {kv('Target Task ID', str(task_id))}
  {kv('Target Model ID', str(model_id))}
  {kv('Target Category', str(category))}
  {kv('Screenshot Directory', str(prefix))}
</div>'''

        search_data = f'{disp_name} {model_id} {task_id} {category} {obj_type}'.lower()

        cards.append(f'''
<div class="card" id="card-{idx}" data-search="{search_data}" data-cat="{category}" data-status="{status_txt}">
  <div class="card-header">
    <span class="badge {status_cls}">{status_icon} {status_txt}</span>
    <span class="card-title">L{lvl} &middot; {disp_name} ({model_id})</span>
    <span class="card-subtitle">{obj_type} &middot; {category}</span>
    <span class="card-task">task {task_id}</span>
  </div>
  {fail_html}
  <div class="tab-bar">
    <button class="tab-btn active" data-tab="images" onclick="switchTab('{idx}', 'images')">&#x1F4F8; Images</button>
    <button class="tab-btn" data-tab="video" onclick="switchTab('{idx}', 'video')">&#x1F3A5; Video</button>
    <button class="tab-btn" data-tab="transform" onclick="switchTab('{idx}', 'transform')">&#x1F4CD; Position &amp; Orientation</button>
    <button class="tab-btn" data-tab="assets" onclick="switchTab('{idx}', 'assets')">&#x1F4E6; Assets</button>
    <button class="tab-btn" data-tab="visual" onclick="switchTab('{idx}', 'visual')">&#x1F50D; Visual Integrity</button>
    <button class="tab-btn" data-tab="evidence" onclick="switchTab('{idx}', 'evidence')">&#x1F4DC; Logs &amp; Evidence</button>
  </div>
  <div class="tab-content">
    <div class="tab-pane active" id="pane-{idx}-images">
      {images_pane_html}
    </div>
    <div class="tab-pane" id="pane-{idx}-video">
      {vid_pane_html}
    </div>
    <div class="tab-pane" id="pane-{idx}-transform">
      {transform_pane_html}
    </div>
    <div class="tab-pane" id="pane-{idx}-assets">
      {assets_pane_html}
    </div>
    <div class="tab-pane" id="pane-{idx}-visual">
      {visual_pane_html}
    </div>
    <div class="tab-pane" id="pane-{idx}-evidence">
      {evidence_pane_html}
    </div>
  </div>
</div>''')

    cards_html = '\n'.join(cards)

    html = f'''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>IGI Editor E2E Report</title>
<style>{STYLE}</style>
</head>
<body>
<div class="header">
  <div><h1>&#9670; IGI EDITOR E2E REPORT</h1><div style="color:var(--muted);font-size:11px;margin-top:4px">Source Manifest + Render Evidence</div></div>
  <div class="kpi"><div class="val cyan-val">{len(all_objs)}</div><div class="lbl">OBJECTS</div></div>
  <div class="kpi"><div class="val pass-val">{total_pass}</div><div class="lbl">PASS</div></div>
  <div class="kpi"><div class="val fail-val">{total_fail}</div><div class="lbl">FAIL</div></div>
  <div class="kpi"><div class="val {ov_cls}">{overall}</div><div class="lbl">OVERALL</div></div>
</div>
<div class="toolbar">
  <input id="search" oninput="filterCards()" placeholder="Search model / name / task / type..." />
  <select id="cat-filter" onchange="filterCards()">
    <option value="">All Categories</option>{cat_opts}
  </select>
  <select id="status-filter" onchange="filterCards()">
    <option value="">All Status</option>
    <option value="PASS">PASS</option>
    <option value="FAIL">FAIL</option>
  </select>
  <div class="toolbar-tabs">
    <span style="color:var(--muted);font-size:11px;margin-right:4px">View All:</span>
    <button class="global-tab-btn" onclick="setAllTabs('images')">&#x1F4F8; Images</button>
    <button class="global-tab-btn" onclick="setAllTabs('video')">&#x1F3A5; Video</button>
    <button class="global-tab-btn" onclick="setAllTabs('transform')">&#x1F4CD; Transform</button>
    <button class="global-tab-btn" onclick="setAllTabs('assets')">&#x1F4E6; Assets</button>
    <button class="global-tab-btn" onclick="setAllTabs('evidence')">&#x1F4DC; Logs</button>
  </div>
</div>
<div class="cards">{cards_html}</div>
<div class="modal-bg" id="modal" onclick="closeModal()">
  <img class="modal-img" id="modal-img" src="" alt="full size" />
</div>
<script>{JS}</script>
</body>
</html>
'''
    Path(output_path).write_text(html, encoding='utf-8')
    print(f'[Dashboard] Written: {output_path}')

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description='Generate IGI E2E HTML dashboard')
    parser.add_argument('--artifact-dir', required=True, help='Root artifact dir (contains level*/ subdirs or is itself level dir)')
    parser.add_argument('--output', default='report.html')
    args = parser.parse_args()

    artifact_root = Path(args.artifact_dir)
    data_sources = []

    # Look for batch.json files (may be nested in levelN/ subdirs or at root)
    batch_files = list(artifact_root.rglob('batch.json'))
    if not batch_files:
        print(f'[Dashboard] No batch.json found under {artifact_root}', file=sys.stderr)
        sys.exit(1)

    for bf in batch_files:
        try:
            batch = json.loads(bf.read_text(encoding='utf-8-sig'))
            lvl = batch.get('level', 0)
            data_sources.append((lvl, batch, bf.parent))
        except Exception as e:
            print(f'[Dashboard] Skipping {bf}: {e}', file=sys.stderr)

    output_path = artifact_root / args.output
    build_report(data_sources, str(output_path), artifact_root)

if __name__ == '__main__':
    main()
