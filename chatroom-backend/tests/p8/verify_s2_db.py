import hashlib
import json
import subprocess
import sys

MYSQL = ["mysql", "--socket=/run/mysqld/mysqld.sock", "-uroot", "chatroom_p8",
         "-N", "-B", "-e"]
results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else " :: " + str(detail)))


def q(sql):
    out = subprocess.run(MYSQL + [sql], capture_output=True, text=True, check=True)
    return [line.split("\t") for line in out.stdout.strip().splitlines() if line]


meal_count = int(q("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo'")[0][0])
idem_count = int(q("SELECT COUNT(*) FROM idempotency_records WHERE agent_uid='u_demo'")[0][0])
print(f"meal_records={meal_count} idempotency_records={idem_count}")
check("meal rows written to MySQL", meal_count > 0, meal_count)
check("idempotency rows written to MySQL", idem_count > 0, idem_count)
check("one idempotency row per successful write", meal_count == idem_count,
      (meal_count, idem_count))

orphans = int(q("SELECT COUNT(*) FROM idempotency_records i LEFT JOIN meal_records m "
               "ON m.id=i.meal_id AND m.agent_uid=i.agent_uid WHERE m.id IS NULL")[0][0])
check("no orphan idempotency records", orphans == 0, orphans)

dups = int(q("SELECT COUNT(*) FROM (SELECT agent_uid,idem_key FROM idempotency_records "
             "GROUP BY 1,2 HAVING COUNT(*)>1) t")[0][0])
check("no duplicate (agent_uid,idem_key)", dups == 0, dups)

empty_hash = int(q("SELECT COUNT(*) FROM idempotency_records WHERE CHAR_LENGTH(payload_hash)<>64")[0][0])
check("all payload_hash are 64 hex chars", empty_hash == 0, empty_hash)

bad_date = int(q("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo' "
                 "AND (`date` < '2000-01-01' OR `date` > '2100-12-31')")[0][0])
check("all meal dates in allowed range", bad_date == 0, bad_date)

damaged = int(q("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo' "
                "AND (protein<0 OR fiber<0 OR veggies<0 OR water<0)")[0][0])
check("no negative nutrition values", damaged == 0, damaged)

pen = q("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo' AND text='奶茶配炸鸡' AND penalty=7")
check("penalty 7 persisted for 奶茶炸鸡", pen and int(pen[0][0]) >= 1, pen)
pen0 = q("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo' AND text='青菜豆腐' AND penalty=0")
check("penalty 0 persisted for clean text", pen0 and int(pen0[0][0]) >= 1, pen0)

rows = q("SELECT i.idem_key,i.payload_hash,m.date,m.text,m.type "
         "FROM idempotency_records i JOIN meal_records m ON m.id=i.meal_id "
         "WHERE i.agent_uid='u_demo' ORDER BY i.id DESC LIMIT 8")
checked = 0
mismatch = []
for idem_key, stored_hash, mdate, mtext, mtype in rows:
    canonical = json.dumps({"date": mdate, "text": mtext, "type": mtype},
                           ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    computed = hashlib.sha256(canonical.encode("utf-8")).hexdigest()
    checked += 1
    if computed != stored_hash:
        mismatch.append((idem_key, canonical, computed, stored_hash))
check("payload_hash == SHA256(canonical{date,text,type})", checked > 0 and not mismatch,
      mismatch)

sample = q("SELECT id,date,type,time,text,items,protein,fiber,veggies,water,penalty "
           "FROM meal_records WHERE agent_uid='u_demo' ORDER BY id DESC LIMIT 3")
for row in sample:
    print("ROW " + " | ".join(row))
check("items column holds JSON array", all(r[5].startswith("[") for r in sample), sample[:1])
check("time column HH:MM", all(len(r[3]) == 5 and r[3][2] == ":" for r in sample), sample[:1])

passed = sum(1 for _, ok in results if ok)
failed = len(results) - passed
print(f"SUMMARY passed={passed} failed={failed} total={len(results)}")
sys.exit(0 if failed == 0 else 1)