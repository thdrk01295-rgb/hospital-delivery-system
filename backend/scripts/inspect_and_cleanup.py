"""
Dev/admin inspection and cleanup script.

Run from the backend/ directory on the machine where MSSQL is accessible:
    python -m scripts.inspect_and_cleanup              # read-only inspection
    python -m scripts.inspect_and_cleanup --cancel-stale   # also cancel stale tasks
    python -m scripts.inspect_and_cleanup --fix-codes      # also fix old location_codes if migration wasn't applied

Requirements: pip install sqlalchemy pyodbc pydantic-settings
              Microsoft ODBC Driver 17 for SQL Server must be installed.
"""
import sys
import os
import argparse
from datetime import datetime, timezone

# Allow running from backend/ as  python -m scripts.inspect_and_cleanup
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

try:
    from app.config.settings import settings
    from app.db.session import SessionLocal
    from app.models.location import Location
    from app.models.task import Task
    from app.constants.enums import TaskStatus, LocationType
    _APP_IMPORTS_OK = True
except Exception as _import_err:
    _APP_IMPORTS_OK = False
    _import_err_msg = str(_import_err)

# Canonical codes expected after migration 0003
EXPECTED_CODES = {
    "station":      "STATION-01",
    "laundry":      "LAUNDRY-01",
    "warehouse":    "WAREHOUSE-01",
    "specimen_lab": "SPECIMEN-LAB",
    "exam_A":       "EXAM-A",
    "exam_B":       "EXAM-B",
    "exam_C":       "EXAM-C",
}
NON_BED_TYPES = list(EXPECTED_CODES.keys())
ACTIVE_STATUSES = ("PENDING", "DISPATCHED", "IN_PROGRESS")


def sep(title=""):
    width = 72
    if title:
        print(f"\n{'─' * 4} {title} {'─' * max(0, width - 6 - len(title))}")
    else:
        print("─" * width)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cancel-stale", action="store_true",
                        help="Mark stale test tasks CANCELLED (non-dry-run)")
    parser.add_argument("--fix-codes", action="store_true",
                        help="Fix old location_codes in-place (use if migration 0003 wasn't applied)")
    parser.add_argument("--stale-hours", type=float, default=1.0,
                        help="Age threshold in hours for 'stale' tasks (default: 1)")
    args = parser.parse_args()

    if not _APP_IMPORTS_OK:
        print(f"ERROR: Could not import app modules: {_import_err_msg}")
        print()
        print("Make sure you are running from the backend/ directory and all")
        print("requirements are installed:")
        print("  pip install sqlalchemy pyodbc pydantic-settings alembic")
        print("  # Also: ODBC Driver 17 for SQL Server must be installed")
        print()
        print("Run with:")
        print("  cd backend")
        print("  python -m scripts.inspect_and_cleanup")
        sys.exit(1)

    # ── 1. Connection target (no password) ───────────────────────────────────
    sep("1. DATABASE CONNECTION")
    safe_url = (
        f"mssql+pyodbc://{settings.DB_USER}:***"
        f"@{settings.DB_HOST}:{settings.DB_PORT}/{settings.DB_NAME}"
        "?driver=ODBC+Driver+17+for+SQL+Server"
    )
    print(f"  Target: {safe_url}")

    db = SessionLocal()
    try:
        import sqlalchemy

        # ── 2. Alembic current revision ──────────────────────────────────────
        sep("2. ALEMBIC REVISION")
        try:
            rows = db.execute(
                sqlalchemy.text("SELECT version_num FROM alembic_version")
            ).fetchall()
            if rows:
                revisions = [r[0] for r in rows]
                print(f"  Current revision(s): {revisions}")
                if "0003" in revisions:
                    print("  ✓ Migration 0003 (location_code fix) is applied.")
                else:
                    print("  ✗ Migration 0003 NOT applied.")
                    print("    Run:  alembic upgrade head")
                    print("    Or pass --fix-codes to this script to patch in-place.")
            else:
                print("  alembic_version table is empty — no migrations recorded.")
        except Exception as e:
            print(f"  Could not query alembic_version: {e}")

        # ── 3. Non-bed locations ─────────────────────────────────────────────
        sep("3. NON-BED LOCATIONS  (id / location_type / location_code / display_name)")
        locations = (
            db.query(Location)
            .filter(Location.location_type.in_(NON_BED_TYPES))
            .order_by(Location.id)
            .all()
        )

        needs_fix = []
        print(f"  {'ID':>4}  {'location_type':<15}  {'location_code':<15}  {'display_name':<20}  Check")
        print(f"  {'─'*4}  {'─'*15}  {'─'*15}  {'─'*20}  {'─'*20}")
        for loc in locations:
            expected = EXPECTED_CODES.get(loc.location_type)
            ok = loc.location_code == expected
            status = "✓ OK" if ok else f"✗ want '{expected}'"
            if not ok:
                needs_fix.append((loc, expected))
            print(f"  {loc.id:>4}  {loc.location_type:<15}  {loc.location_code:<15}  "
                  f"{(loc.display_name or ''):<20}  {status}")

        # Check all 7 expected codes
        sep("3b. CANONICAL CODE PRESENCE CHECK")
        found_codes = {loc.location_code for loc in locations}
        all_ok = True
        for loc_type, code in EXPECTED_CODES.items():
            present = code in found_codes
            if not present:
                all_ok = False
            print(f"  {'✓' if present else '✗'}  {code:<15}  ({'found' if present else 'MISSING'})")

        # ── 4. Fix stale codes ───────────────────────────────────────────────
        sep("4. LOCATION CODE FIX")
        if not needs_fix:
            print("  All non-bed location_code values are correct — no fix needed.")
        elif args.fix_codes:
            for loc, new_code in needs_fix:
                old = loc.location_code
                loc.location_code = new_code
                print(f"  Updated id={loc.id}: '{old}' → '{new_code}'")
            db.commit()
            print(f"  ✓ {len(needs_fix)} location(s) patched.")
        else:
            print(f"  {len(needs_fix)} location(s) have wrong codes (listed above).")
            print("  To fix: pass --fix-codes, or run:  alembic upgrade head")

        # ── 5. Active tasks ──────────────────────────────────────────────────
        sep("5. ACTIVE TASKS  (PENDING / DISPATCHED / IN_PROGRESS)")

        # Rebuild location_code lookup from fresh query (in case fix above ran)
        db.expire_all()
        active_tasks = (
            db.query(Task)
            .filter(Task.status.in_(list(ACTIVE_STATUSES)))
            .order_by(Task.created_at.asc())
            .all()
        )

        if not active_tasks:
            print("  No active tasks found.")
        else:
            hdr = (f"  {'ID':>4}  {'task_type':<25}  {'status':<14}  "
                   f"{'pri':>3}  {'origin':<15}  {'destination':<15}  created_at")
            print(hdr)
            print("  " + "─" * (len(hdr) - 2))
            for t in active_tasks:
                origin_code = (t.origin_location.location_code
                               if t.origin_location else "(none)")
                dest_code   = (t.destination_location.location_code
                               if t.destination_location else "(none)")
                created     = (t.created_at.strftime("%Y-%m-%d %H:%M:%S")
                               if t.created_at else "?")
                print(f"  {t.id:>4}  {t.task_type:<25}  {t.status:<14}  "
                      f"{t.priority:>3}  {origin_code:<15}  {dest_code:<15}  {created}")

        # ── 6. Stale task detection ──────────────────────────────────────────
        sep(f"6. STALE TASKS  (age >= {args.stale_hours:.0f} h)")
        if not active_tasks:
            print("  No active tasks to evaluate.")
        else:
            now_utc = datetime.now(timezone.utc)
            stale = []
            for t in active_tasks:
                if t.created_at:
                    ca = (t.created_at.replace(tzinfo=timezone.utc)
                          if t.created_at.tzinfo is None else t.created_at)
                    age_h = (now_utc - ca).total_seconds() / 3600
                    if age_h >= args.stale_hours:
                        stale.append((t, age_h))

            if not stale:
                print(f"  No tasks older than {args.stale_hours:.0f} h — nothing to cancel.")
            else:
                print(f"  Found {len(stale)} stale task(s):")
                for t, age in stale:
                    origin_code = (t.origin_location.location_code
                                   if t.origin_location else "(none)")
                    dest_code   = (t.destination_location.location_code
                                   if t.destination_location else "(none)")
                    print(f"    id={t.id:<4}  type={t.task_type:<25}  status={t.status:<14}  "
                          f"age={age:.1f}h  {origin_code} → {dest_code}")

                if args.cancel_stale:
                    print()
                    for t, age in stale:
                        t.status = "CANCELLED"
                        print(f"  → Cancelled id={t.id} ({t.task_type}, age={age:.1f}h)")
                    db.commit()
                    print(f"\n  ✓ {len(stale)} stale task(s) marked CANCELLED.")
                else:
                    print()
                    print("  DRY RUN — to cancel them, pass --cancel-stale")

        # ── 7. All task counts ───────────────────────────────────────────────
        sep("7. TASK COUNT BY STATUS")
        rows = db.execute(
            sqlalchemy.text(
                "SELECT status, COUNT(*) AS cnt FROM tasks GROUP BY status ORDER BY cnt DESC"
            )
        ).fetchall()
        for row in rows:
            print(f"  {row[0]:<20}  {row[1]}")

    except Exception as e:
        print(f"\nFATAL ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        db.close()

    sep()
    print("  Done.")
    sep()


if __name__ == "__main__":
    main()
