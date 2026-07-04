from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse
from openai import OpenAI
import httpx

app = FastAPI()

ARMADA_BASE_URL = "https://35.212.149.5/v1"
ARMADA_API_KEY = "arm_live_lOdV4X_fbvsdOPej654oEVxDqwKnqZXqTWtKgYqbPes"

client = OpenAI(
    base_url=ARMADA_BASE_URL,
    api_key=ARMADA_API_KEY,
    http_client=httpx.Client(verify=False)
)

# Struktur penyimpanan memori diperbarui agar menampung objek terstruktur
data_iot_terakhir = {
    "bpm": 0,
    "spo2": 0,
    "suhu": 0.0,
    "status": "Normal",
    "saran": "Menunggu transmisi data telemetri pertama dari perangkat IoT medis..."
}

@app.get("/", response_class=HTMLResponse)
async def baca_dashboard():
    with open("index.html", "r") as file:
        return file.read()

@app.get("/api/data")
async def ambil_data():
    return data_iot_terakhir

@app.post("/api/kirim-data")
async def terima_data(request: Request):
    global data_iot_terakhir
    payload = await request.json()
    
    bpm = payload.get("bpm", 0)
    spo2 = payload.get("spo2", 0)
    suhu = payload.get("suhu", 0.0)
    
    # Memanggil fungsi structured output JSON
    objek_medis = panggil_armada_gateway_json(bpm, spo2, suhu)
    
    data_iot_terakhir = {
        "bpm": bpm,
        "spo2": spo2,
        "suhu": suhu,
        "status": objek_medis.get("status", "Normal"),
        "saran": objek_medis.get("saran", "Tidak ada saran.")
    }
    
    # Return string teks gabungan untuk dibaca oleh ESP32 sebagai trigger buzzer alarm
    teks_respon_esp = f"status: {data_iot_terakhir['status']}. saran: {data_iot_terakhir['saran']}"
    return teks_respon_esp

def simulasi_analisis_medis(bpm: int, spo2: int, suhu: float):
    # Rule-based engine untuk klasifikasi status kesehatan (simulasi lokal)
    if spo2 < 95 and spo2 > 0:
        if bpm > 100:
            status = "Kondisi Darurat"
            saran = "Kadar oksigen rendah dan detak jantung tinggi. Segera cari pertolongan medis dan gunakan oksigen tambahan."
        else:
            status = "Gangguan Pernapasan"
            saran = f"Saturasi oksigen rendah ({spo2}%). Istirahat di tempat dengan sirkulasi udara baik dan hindari aktivitas fisik."
    elif bpm > 110:
        status = "Takikardia"
        saran = "Detak jantung sangat cepat. Cobalah duduk tenang, lakukan pernapasan dalam, dan hindari kafein atau stres."
    elif bpm > 100:
        status = "Stres / Kelelahan"
        saran = "Detak jantung meningkat ringan. Istirahatlah sejenak, minum air hangat, dan rilekskan pikiran."
    elif bpm < 60 and bpm > 0:
        status = "Bradikardia"
        saran = "Detak jantung di bawah normal. Jika Anda merasa pusing atau lemas, segera periksakan diri ke dokter."
    else:
        status = "Sehat"
        saran = "Seluruh parameter vital Anda dalam keadaan sehat dan normal. Jaga pola tidur dan makan teratur."

    return {
        "status": f"{status} (Simulasi)",
        "saran": saran
    }

def panggil_armada_gateway_json(bpm: int, spo2: int, suhu: float):
    if bpm == 0 and suhu == 0:
        return {"status": "Normal", "saran": "Menunggu transmisi data telemetri pertama dari perangkat IoT medis..."}

    # Cek jika API Key masih default/placeholder
    if not ARMADA_API_KEY or "ISI_DENGAN" in ARMADA_API_KEY:
        print("API Key masih default. Menggunakan analisis medis simulasi lokal...")
        return simulasi_analisis_medis(bpm, spo2, suhu)

    try:
        # Memaksa Armada mengeluarkan skema JSON terstruktur (Sesuai Step D dokumentasi)
        response = client.chat.completions.create(
            model="gemini-2.5-flash", 
            messages=[
                {
                    "role": "system", 
                    "content": "Kamu adalah dokter ahli asisten medis pintar. Analisis data vital pasien (BPM dan SpO2) dan klasifikasikan status kesehatan mereka secara eksklusif ke salah satu kategori dari list berikut: Sehat, Stres / Kelelahan, Gangguan Pernapasan, Takikardia, Bradikardia, atau Kondisi Darurat. Kembalikan respons dalam format JSON."
                },
                {
                    "role": "user", 
                    "content": f"BPM: {bpm}, SpO2: {spo2}%."
                }
            ],
            response_format={
                "type": "json_schema",
                "schema": {
                    "type": "object",
                    "properties": {
                        "status": {
                            "type": "string",
                            "enum": ["Sehat", "Stres / Kelelahan", "Gangguan Pernapasan", "Takikardia", "Bradikardia", "Kondisi Darurat"],
                            "description": "Klasifikasi status kesehatan pasien."
                        },
                        "saran": {
                            "type": "string",
                            "description": "Saran medis solutif singkat maksimal 2 kalimat sesuai dengan klasifikasi status."
                        }
                    },
                    "required": ["status", "saran"]
                }
            },
            stream=False
        )
        
        # Mengubah teks string JSON dari LLM menjadi dictionary Python murni
        import json
        return json.loads(response.choices[0].message.content.strip())
        
    except Exception as e:
        print(f"Error saat memanggil Armada Gateway: {e}. Melakukan fallback ke simulasi lokal...")
        return simulasi_analisis_medis(bpm, spo2, suhu)