FROM python:3.12-slim
WORKDIR /service
COPY server/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY server/service.py .
ENV BIND=0.0.0.0 PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
USER 65534:65534
CMD ["python", "service.py"]
