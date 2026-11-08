from flask import Flask, request, jsonify, render_template
import sqlite3
from datetime import datetime, timedelta

app = Flask(__name__)
DB = "sensor_data.db"

# Устройство считается онлайн, если данные приходили не позже этого времени
ONLINE_THRESHOLD = timedelta(minutes=1)


def get_db():
    conn = sqlite3.connect(DB)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    with get_db() as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS readings (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                device_id TEXT NOT NULL,
                temperature REAL,
                humidity REAL,
                error TEXT,
                timestamp TEXT NOT NULL
            )
        """)
        # Индекс ускоряет выборку последних записей по устройству
        conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_device_ts
            ON readings (device_id, timestamp)
        """)


@app.route("/")
def index():
    return render_template("index.html")


# ---------- Приём данных от устройства ----------
@app.route("/api/data", methods=["POST"])
def receive_data():
    data = request.get_json(force=True, silent=True)
    if not data or "device_id" not in data:
        return jsonify({"error": "invalid payload"}), 400

    with get_db() as conn:
        conn.execute(
            "INSERT INTO readings (device_id, temperature, humidity, error, timestamp) "
            "VALUES (?, ?, ?, ?, ?)",
            (
                data["device_id"],
                data.get("temperature"),
                data.get("humidity"),
                data.get("error"),
                datetime.utcnow().isoformat(),
            ),
        )
    return jsonify({"status": "ok"}), 201


# ---------- Последние N записей для таблицы ----------
@app.route("/api/data")
def get_data():
    limit = min(int(request.args.get("limit", 50)), 500)
    with get_db() as conn:
        rows = conn.execute(
            "SELECT device_id, temperature, humidity, error, timestamp "
            "FROM readings ORDER BY id DESC LIMIT ?",
            (limit,),
        ).fetchall()

    return jsonify([
        {
            "device_id": r["device_id"],
            "temperature": r["temperature"],
            "humidity": r["humidity"],
            "error": r["error"],
            "timestamp": r["timestamp"],
        }
        for r in rows
    ])


# ---------- Статус устройств (online/offline) ----------
@app.route("/api/devices")
def devices_status():
    with get_db() as conn:
        rows = conn.execute("""
            SELECT device_id, MAX(timestamp) AS last_ts, COUNT(*) AS cnt
            FROM readings
            GROUP BY device_id
        """).fetchall()

    now = datetime.utcnow()
    result = []
    for r in rows:
        last = datetime.fromisoformat(r["last_ts"])
        online = (now - last) < ONLINE_THRESHOLD
        result.append({
            "device_id": r["device_id"],
            "last_seen": r["last_ts"],
            "online": online,
            "readings": r["cnt"],
            "seconds_ago": int((now - last).total_seconds()),
        })
    # Сортируем: онлайн-устройства сверху
    result.sort(key=lambda x: (not x["online"], x["device_id"]))
    return jsonify(result)


# ---------- Health-check для внешнего мониторинга ----------
@app.route("/health")
def health():
    try:
        with get_db() as conn:
            conn.execute("SELECT 1").fetchone()
        db_ok = True
    except Exception:
        db_ok = False

    status_code = 200 if db_ok else 503
    return jsonify({
        "status": "ok" if db_ok else "error",
        "db": db_ok,
        "time": datetime.utcnow().isoformat(),
    }), status_code


if __name__ == "__main__":
    init_db()
    app.run(host="0.0.0.0", port=5000, debug=True)