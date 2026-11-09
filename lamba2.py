import json
import logging
import boto3
from datetime import datetime, timezone


# Configurar logs
logger = logging.getLogger()
logger.setLevel(logging.INFO)


dynamodb = boto3.resource('dynamodb')
# Asegúrate de crear esta nueva tabla en DynamoDB con 'shipment_id' como Clave de partición (String)
table = dynamodb.Table('TablaLogistica') 


def lambda_handler(event, context):
    try:
        # 1. Extraer payload (API Gateway o prueba) 
        if 'body' in event and event['body'] is not None:
            body_str = event['body'] if isinstance(event['body'], str) else json.dumps(event['body'])
            payload = json.loads(body_str)
        else:
            payload = json.loads(json.dumps(event))


        # 2. Validación estricta: Garantizar que el Primary Key viene en el JSON
        if 'shipment_id' not in payload or not payload['shipment_id']:
            return {
                'statusCode': 400,
                'body': json.dumps({'error': 'Discrepancia de datos: Falta el atributo obligatorio shipment_id'})
            }


        # 3. Validar la llave foránea lógica (device_id) para el enlace con Bus-TrackerDB
        if 'device_id' not in payload:
            logger.warning(f"Advertencia: El envío {payload['shipment_id']} no tiene un device_id asignado.")


        # 4. Añadir marca de tiempo del registro logístico
        payload['registro_creado_en'] = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')


        # 5. Limpiar valores nulos para evitar errores en DynamoDB
        payload = {k: v for k, v in payload.items() if v is not None}


        # 6. Guardar el registro de logística en DynamoDB
        table.put_item(Item=payload)
        
        logger.info(f"--- Registro Logístico Guardado ---")
        logger.info(f"Envío ID: {payload['shipment_id']} | Dispositivo Enlazado: {payload.get('device_id', 'N/A')}")
        logger.info(f"Ruta: {payload.get('origen', 'N/A')} -> {payload.get('destino', 'N/A')} | Estatus: {payload.get('status', 'N/A')}")


        # 7. Retornar éxito
        return {
            'statusCode': 200,
            'headers': {'Content-Type': 'application/json'},
            'body': json.dumps({
                'message': 'Registro logístico creado/actualizado correctamente',
                'shipment_id': payload['shipment_id'],
                'dispositivo_enlazado': payload.get('device_id', 'Ninguno')
            })
        }


    except Exception as e:
        logger.error(f"Error procesando logística: {str(e)}")
        return {
            'statusCode': 500,
            'body': json.dumps({'error': 'Error interno al procesar el registro logístico'})
        }