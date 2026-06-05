-- ============================================================
-- Hospital AMR — DB Inspection & Cleanup Queries
-- Run in SSMS / Azure Data Studio against hospital_amr DB
-- ============================================================

-- ── 1. Alembic current revision ─────────────────────────────
SELECT version_num FROM alembic_version;

-- ── 2. Non-bed locations with location_code ─────────────────
SELECT
    id,
    location_type,
    location_code,
    display_name,
    CASE
        WHEN location_type = 'station'      AND location_code = 'STATION-01'  THEN 'OK'
        WHEN location_type = 'laundry'      AND location_code = 'LAUNDRY-01'  THEN 'OK'
        WHEN location_type = 'warehouse'    AND location_code = 'WAREHOUSE-01' THEN 'OK'
        WHEN location_type = 'specimen_lab' AND location_code = 'SPECIMEN-LAB' THEN 'OK'
        WHEN location_type = 'exam_A'       AND location_code = 'EXAM-A'      THEN 'OK'
        WHEN location_type = 'exam_B'       AND location_code = 'EXAM-B'      THEN 'OK'
        WHEN location_type = 'exam_C'       AND location_code = 'EXAM-C'      THEN 'OK'
        ELSE 'WRONG'
    END AS code_status
FROM locations
WHERE location_type IN ('station','laundry','warehouse','specimen_lab','exam_A','exam_B','exam_C')
ORDER BY id;

-- ── 3. Fix old location_codes (migration 0003 equivalent) ───
-- Run this block only if migration 0003 has NOT been applied
-- and the check above shows any WRONG rows.
/*
UPDATE locations SET location_code = 'STATION-01'  WHERE location_code = 'station';
UPDATE locations SET location_code = 'LAUNDRY-01'  WHERE location_code = 'laundry';
UPDATE locations SET location_code = 'WAREHOUSE-01' WHERE location_code = 'warehouse';
UPDATE locations SET location_code = 'SPECIMEN-LAB' WHERE location_code = 'specimen_lab';
UPDATE locations SET location_code = 'EXAM-A'       WHERE location_code = 'exam_A';
UPDATE locations SET location_code = 'EXAM-B'       WHERE location_code = 'exam_B';
UPDATE locations SET location_code = 'EXAM-C'       WHERE location_code = 'exam_C';
*/

-- ── 4. Active tasks with resolved location_codes ────────────
SELECT
    t.id,
    t.task_type,
    t.status,
    t.priority,
    ol.location_code  AS origin_code,
    dl.location_code  AS destination_code,
    t.patient_bed_code,
    t.requested_by_role,
    t.created_at
FROM tasks t
LEFT JOIN locations ol ON ol.id = t.origin_location_id
LEFT JOIN locations dl ON dl.id = t.destination_location_id
WHERE t.status IN ('PENDING','DISPATCHED','IN_PROGRESS')
ORDER BY t.created_at ASC;

-- ── 5. Task count by status ──────────────────────────────────
SELECT status, COUNT(*) AS cnt
FROM tasks
GROUP BY status
ORDER BY cnt DESC;

-- ── 6. Cancel stale test tasks ───────────────────────────────
-- Preview first (read-only):
SELECT id, task_type, status, created_at
FROM tasks
WHERE status IN ('PENDING','DISPATCHED','IN_PROGRESS')
  AND created_at < DATEADD(HOUR, -1, GETUTCDATE())
ORDER BY created_at ASC;

-- Then run to actually cancel (uncomment when ready):
/*
UPDATE tasks
SET status = 'CANCELLED'
WHERE status IN ('PENDING','DISPATCHED','IN_PROGRESS')
  AND created_at < DATEADD(HOUR, -1, GETUTCDATE());
*/
