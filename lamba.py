import json
import logging
import boto3
import uuid
from datetime import datetime, timezone
from decimal import Decimal

logger = logging.getLogger()
logger.setLevel(logging.INFO)

dynamodb = boto3.resource('dynamodb')
# Actualizado con el nuevo nombre de tu tabla
table = dynamodb.Table('Bus-TrackerDB') 

def lambda_handler(event, context):
    try:
        # 1. Extraer y convertir el payload (API Gateway o prueba directa)
        if 'body' in event and event['body'] is not None:
            body_str = event['body'] if isinstance(event['body'], str) else json.dumps(event['body'])
            payload = json.loads(body_str, parse_float=Decimal)
        else:
            payload = json.loads(json.dumps(event), parse_float=Decimal)

        # 2. ADAPTACIÓN DE CLAVE: Convertir 'device_id' a 'numCamion' para DynamoDB
        if 'device_id' in payload:
            payload['numCamion'] = payload.pop('device_id')

        # 3. Enriquecer el JSON: El ESP32 no envía ID ni Fecha, la nube se los asigna
        if 'measurement_id' not in payload:
            payload['measurement_id'] = str(uuid.uuid4())
        
        if 'captured_at' not in payload:
            # Generar timestamp UTC actual en formato ISO 8601
            payload['captured_at'] = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')

        # 4. Limpiar valores nulos (null de C++) para evitar problemas en DynamoDB
        payload = {k: v for k, v in payload.items() if v is not None}

        # 5. Logs para CloudWatch
        num_camion = payload.get('numCamion', 'Desconocido')
        logger.info(f"--- Evento recibido de la placa ESP32 (Camión: {num_camion}) ---")
        logger.info(f"ID Registro: {payload.get('measurement_id')} | Fecha: {payload.get('captured_at')}")
        logger.info(f"Temp: {payload.get('temperature', 'N/A')}°C | Hum: {payload.get('humidity', 'N/A')}%")
        logger.info(f"Distancia: {payload.get('distance_cm', 'N/A')} cm")
        logger.info(f"GPS: Lat {payload.get('latitude', 'N/A')}, Lon {payload.get('longitude', 'N/A')}")

        # 6. Guardar en la base de datos
        table.put_item(Item=payload)
        logger.info("Registro almacenado exitosamente en DynamoDB")

        # 7. Retornar respuesta HTTP 200 al ESP32
        return {
            'statusCode': 200,
            'headers': {'Content-Type': 'application/json'},
            'body': json.dumps({
                'message': 'Datos insertados correctamente en Bus-TrackerDB',
                'measurement_id': payload.get('measurement_id')
            })
        }

    except Exception as e:
        logger.error(f"Error procesando el payload: {str(e)}")
        return {
            'statusCode': 500,
            'body': json.dumps({'error': 'Error interno al procesar los datos'})
        }