FROM alpine:3.23
RUN apk add --no-cache nodejs npm
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --omit=dev
COPY index.js config.js ./
CMD ["node","index.js"]
