FROM python:3.14-slim
RUN pip3 install pyfirmata paho-mqtt
ADD gateway.py .
CMD ["python3", "-u", "./gateway.py"]
