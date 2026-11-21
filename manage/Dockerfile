FROM node:22-bookworm AS web
WORKDIR /web
COPY manage/web/package.json manage/web/package-lock.json ./
RUN npm ci
COPY manage/web ./
RUN npm run build

FROM golang:1.25-bookworm AS server
WORKDIR /src
COPY manage/go.mod manage/go.sum ./
RUN go mod download
COPY manage/cmd ./cmd
COPY manage/internal ./internal
RUN CGO_ENABLED=0 GOOS=linux go build -o /lanroom-manage ./cmd/lanroom-manage

FROM debian:bookworm-slim
WORKDIR /app
COPY --from=server /lanroom-manage /usr/local/bin/lanroom-manage
COPY --from=web /web/dist ./web/dist
ENV PANEL_DIST=/app/web/dist
EXPOSE 8080
CMD ["lanroom-manage"]
