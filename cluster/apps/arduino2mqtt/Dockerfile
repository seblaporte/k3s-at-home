FROM python:3.12-slim
RUN pip3 install pyfirmata paho-mqtt
ADD gateway.py .
CMD ["python3", "-u", "./gateway.py"]
