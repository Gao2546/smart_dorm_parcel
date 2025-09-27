# Use official Node.js LTS image
FROM node:20-alpine

# Install openssl (ต้องใช้สร้าง cert)
RUN apk add --no-cache openssl bash

# Set working directory
WORKDIR /app

# Copy package.json and package-lock.json
COPY package.json package-lock.json ./

# Install dependencies
RUN npm install

# Copy the rest of the project files
COPY . .

# Build TypeScript code
RUN npm run build

# Create certs folder and generate self-signed certificate
RUN mkdir certs && \
    openssl req -nodes -new -x509 \
        -keyout certs/server.key \
        -out certs/server.cert \
        -days 365 \
        -subj "/C=TH/ST=Bangkok/L=Bangkok/O=Dev/CN=localhost"

# Expose HTTPS port
EXPOSE 3000

# Start the server
CMD ["node", "build/index.js"]
