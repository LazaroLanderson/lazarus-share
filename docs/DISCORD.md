# Integração com Bot no Discord e Webhook

Esta documentação detalha a configuração e uso do **Bot no Discord** e da funcionalidade de anúncio por **Webhook** no **Lazarus Share**.

---

## 1. Como Funciona a Integração

### 1.1 Início de Sala via Comando no Discord (`/share`)
1. Um usuário conectado a um canal de voz no Discord digita `/share` (ou `/lazarus`).
2. O Bot consulta a API do Discord e obtém todos os membros conectados na mesma call.
3. O Bot conecta como coordenador no servidor de sinalização do Lazarus Share (`/ws`) e cria a sala com aprovação automática.
4. O Bot envia no chat da call um Embed detalhado com:
   - Menção direta a todos os participantes da chamada.
   - Botão interativo **`[🚀 Entrar na Transmissão]`**.
   - Botão **`[📥 Baixar Aplicativo]`** (para quem ainda não tiver o app instalado).
5. Ao clicar em **`[🚀 Entrar na Transmissão]`**, o navegador abre a página do convite e dispara imediatamente o protocolo `lazarus-share://join/<TOKEN>`:
   - Se o Lazarus Share já estiver aberto, ele entra na sala instantaneamente via IPC (`Activation`).
   - Se estiver fechado, o sistema operacional inicia o executável e se conecta à sala.
6. Qualquer usuário na sala pode clicar em "Compartilhar Tela" no app para se tornar o transmissor.
7. O Bot monitora a sala em tempo real:
   - Atualiza o Embed no Discord indicando quem está transmitindo ao vivo.
   - Mostra o contador de espectadores na sala (até 5 participantes simultâneos).
8. Quando a chamada de voz fica vazia, ou quando o comando `/encerrar` é executado, a sala é finalizada automaticamente.

---

## 2. Configurando o Bot no Discord Developer Portal

1. Acesse o [Discord Developer Portal](https://discord.com/developers/applications).
2. Clique em **New Application** e escolha o nome (ex: `Lazarus Share`).
3. Vá para a aba **Bot** e clique em **Reset Token** para gerar o `DISCORD_BOT_TOKEN`. Guarde esse valor.
4. Na seção **Privileged Gateway Intents**:
   - Ative **Server Members Intent** (opcional, para resolução detalhada de perfis).
5. Vá para a aba **OAuth2** -> **URL Generator**:
   - Em **Scopes**, marque:
     - `bot`
     - `applications.commands` (obrigatório para comandos Slash `/share` e `/encerrar`)
   - Em **Bot Permissions**, marque:
     - `Send Messages`
     - `Embed Links`
     - `Attach Files`
     - `Use External Emojis`
     - `View Channel`
6. Copie a URL gerada e abra no seu navegador para adicionar o bot ao seu servidor do Discord.

---

## 3. Executando o Bot

### Opção A: Pelo GitHub Secrets (Sem precisar de SSH na VPS)
Se você não tem acesso SSH direto à VPS agora, pode configurar diretamente no GitHub:
1. No seu repositório no GitHub, vá em **Settings** > **Secrets and variables** > **Actions**.
2. Na aba **Secrets**, clique em **New repository secret**.
3. Nome: `DISCORD_BOT_TOKEN`
4. Valor: cole o token do bot gerado no Discord Developer Portal.
5. Clique em **Add secret**.
6. Quando o próximo commit for enviado para a branch `main` (ou executando manualmente o workflow **Deploy VPS** em Actions), o script de deploy configurará automaticamente o token na VPS e iniciará o container do Bot!

### Opção B: Direto na VPS via Docker Compose (Com SSH)
```bash
cd bot
pip install -r requirements.txt

export DISCORD_BOT_TOKEN="seu_token_aqui"
export SIGNAL_URL="wss://share.app.lazaruslabs.com.br/ws"
export WEB_JOIN_BASE_URL="https://share.app.lazaruslabs.com.br/join"

python main.py
```

---

## 4. Anúncio no Discord pelo Aplicativo Desktop (Webhook)

Para anfitriões que preferem iniciar a sala primeiro pelo aplicativo desktop:
1. Abra o **Lazarus Share** e clique em **Criar sala**.
2. No banner do convite, clique no botão roxo **Anunciar no Discord**.
3. Se for a primeira vez, o aplicativo solicitará a URL do Webhook do canal do Discord:
   - No Discord: Clique na engrenagem do canal de texto > **Integrações** > **Webhooks** > **Novo Webhook** > **Copiar URL do Webhook**.
4. O app enviará uma mensagem formatada com Embed no canal com o link de acesso direto à transmissão.
5. Você também pode alterar ou salvar o Webhook em **Configurações** na seção **INTEGRAÇÃO DISCORD**.
