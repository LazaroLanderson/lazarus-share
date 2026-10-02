# Hospedar salas e relay

Necessário: servidor Linux com Docker Compose, dois endereços IPv4 públicos
atribuídos à máquina, dois nomes DNS e certificados TLS válidos. Alternativamente,
execute Coturn numa segunda máquina, ajustando `TURN_HOST` e o segredo igual nos
dois serviços. O exemplo supõe IPs diretamente atribuídos, sem NAT na VPS.

1. Copie `infra/.env.example` para `infra/.env`. Substitua IPs e domínio;
   gere `TURN_SECRET` aleatório e privado com o comando indicado. Não publique `.env`.
2. Configure o DNS de sinalização para `SIGNAL_IP` e o de TURN para `TURN_IP`.
3. Coloque `fullchain.pem` e `privkey.pem` em `infra/certs/signal/` e
   `infra/certs/turn/`. Ajuste permissões para leitura pelos containers;
   mantenha as chaves privadas fora do repositório.
4. Abra TCP/443 no IP de sinalização. No IP TURN, abra UDP/TCP/3478,
   TCP/443 e UDP/49160–49200. Não exponha a porta 8080.
5. Execute `docker compose --env-file infra/.env -f infra/compose.yml up -d --build`.
6. Confira `https://seu-servidor/health` e configure o app com esse servidor e STUN.

O serviço gera credenciais TURN válidas por uma hora; não compartilha o segredo
de Coturn. A sala limita quatro viewers, Coturn limita oito allocations por
credencial, 64 no total e 20 MB/s por sessão. Dimensione banda e limites conforme
uso real. A versão inicial exige uma nova sessão para renovar autorização e
credenciais após uma hora; não promete reconexão de relay após esse prazo.

Os IPs distintos permitem WSS e TURN/TLS na porta 443 sem disputa. TURN/TLS
não é HTTP: CDN/proxy HTTP comum não pode ficar na frente dele. Para IPv6,
configure listeners/relay IPv6 e firewall explicitamente; P2P já pode usar
candidatos IPv6 dos PCs mesmo quando o servidor está apenas em IPv4.

Logs de acesso do Nginx, registros de conexão de Coturn e logs persistentes dos
containers estão desligados. O serviço não registra payloads e não usa banco.
O provedor da VPS, DNS, firewall ou sistema operacional pode ter sua própria
retenção: o aplicativo não controla serviços externos ao conjunto configurado.

O exemplo não solicita certificados automaticamente. Renove-os pela ferramenta
de sua infraestrutura e reinicie os containers após atualização. Um reinício
apaga salas em memória e exige nova sala. Há somente uma instância de sinalização;
não colocar réplicas sem um projeto adicional de coordenação.
