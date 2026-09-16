@echo off
title RemoteDiag TCP Tunnel
echo Запуск туннелирования для диагностического сервера...

:: Мы пробрасываем порт 8080 (порт вашего TCP-сервера) через сервер Pinggy
:: Ключ -o StrictHostKeyChecking=no отключает запрос на подтверждение ключа хоста SSH
ssh -o StrictHostKeyChecking=no -p 443 -R 0:localhost:8080 tcp.pinggy.io
