import time
import json
import psutil
import threading
from collections import deque
from flask import Flask, jsonify
import paho.mqtt.client as mqtt

app = Flask(__name__)

# --- Configuración AWS ---
AWS_ENDPOINT = ".....iot.us-east-1.amazonaws.com"
MQTT_TOPIC = "instancia/metricas"
CA_PATH = "./AmazonRootCA1.pem"
CERT_PATH = "./certificate.pem.crt"
KEY_PATH = "./private.pem.key"

# Historial en memoria: Guardamos las últimas 30 lecturas (tomadas cada 2 segund                                                                                       os = 1 minuto de logs)
HISTORIAL_VALORES = 30
historial_cpu = deque(maxlen=HISTORIAL_VALORES)
historial_ram = deque(maxlen=HISTORIAL_VALORES)

# Inicializar cliente MQTT con Callback API V1
mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id="EC2_Bridg                                                                                       e_Server")

def conectar_mqtt():
    try:
        mqtt_client.tls_set(ca_certs=CA_PATH, certfile=CERT_PATH, keyfile=KEY_PA                                                                                       TH)
        mqtt_client.connect(AWS_ENDPOINT, 8883, keepalive=60)
        mqtt_client.loop_start()
        print("Conectado exitosamente a AWS IoT Core.")
    except Exception as e:
        print(f"Aviso MQTT: {e} (El servidor seguirá funcionando localmente para                                                                                        el ESP32)")

# --- HILO EN SEGUNDO PLANO PARA SUAVIZAR DATA ---
def background_logger():
    """ Lee las métricas constantemente cada 2 segundos y calcula el promedio mó                                                                                       vil """
    print("Iniciando recolector de métricas suavizadas...")
    # Inicializar psutil
    psutil.cpu_percent(interval=None)

    while True:
        try:
            # Captura instantánea
            cpu_inst = psutil.cpu_percent(interval=None)
            ram_inst = psutil.virtual_memory().percent

            # Guardar en el buffer circular
            historial_cpu.append(cpu_inst)
            historial_ram.append(ram_inst)

            # Enviar la data instantánea a AWS si está conectado
            stats_inst = {"cpu": cpu_inst, "ram": ram_inst, "temp": 45.0}
            try:
                mqtt_client.publish(MQTT_TOPIC, json.dumps(stats_inst), qos=0)
            except:
                pass

        except Exception as e:
            print(f"Error en recolector: {e}")

        time.sleep(2) # Muestreo cada 2 segundos

# Arrancar el hilo de recolección antes de iniciar Flask
threads_recolector = threading.Thread(target=background_logger, daemon=True)
threads_recolector.start()
conectar_mqtt()

# --- RUTA PARA EL ESP32 ---
@app.route('/obtener-estado', methods=['GET'])
def obtener_estado():
    # Si aún no hay suficientes datos en el buffer, usamos el valor actual
    if len(historial_cpu) == 0:
        cpu_promedio = psutil.cpu_percent(interval=None)
        ram_promedio = psutil.virtual_memory().percent
    else:
        # Calcular el promedio matemático de todo el minuto guardado
        cpu_promedio = sum(historial_cpu) / len(historial_cpu)
        ram_promedio = sum(historial_ram) / len(historial_ram)

    # Devolvemos datos estables y promediados con 1 solo decimal
    stats_estables = {
        "cpu": round(cpu_promedio, 1),
        "ram": round(ram_promedio, 1),
        "temp": 45.0
    }

    return jsonify(stats_estables)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)
ubuntu@ip-172-31-8-93:~/MONITOR$
