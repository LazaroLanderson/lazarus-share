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

## VPS com apenas um IP: salas e P2P

Use `infra/compose.signaling.yml` para começar com HTTPS e sinalização, sem relay.
A captura e mídia continuam nos PCs; a VPS coordena as salas. Configure
`SIGNAL_IP=0.0.0.0` para escutar nas interfaces da VPS, ou com um IP atribuído
à máquina quando quiser limitar a interface. Configure `SIGNAL_HOST` com o domínio das salas,
por exemplo `salas.seudominio.com`. Crie o registro DNS A apontando para o IP público real da VPS
(e AAAA somente se IPv6 também estiver configurado). Os campos TURN do exemplo
não são usados nesse modo.

Coloque o certificado e a chave desse domínio em `infra/certs/signal/`, como
na instalação completa. Libere TCP/443; não publique a porta interna 8080.
Certificados válidos e a porta 443 disponível são pré-requisitos: se a VPS já
hospeda outros sites, adapte o proxy existente antes de usar este exemplo.

```sh
docker compose --env-file infra/.env -f infra/compose.signaling.yml up -d --build --wait
```

Nos dois apps, configure `wss://salas.seudominio.com/ws`, sem pin de certificado
local. Sem Coturn próprio, configure um STUN público de sua escolha nos clientes;
este provedor processará metadados da descoberta. O ngrok deixa de ser necessário.
Esse modo não supera bloqueios de P2P por relay: o servidor retorna
`turn_unavailable` se os participantes pedirem relay.

Para o modo completo, mantenha os dois IPs distintos e use `infra/compose.yml`.
Nos clientes, configure `stun://turn.seudominio.com:3478`. As URLs TURN/TLS
emitidas pelo cliente atual usam a porta 443. Não basta colocar Coturn em 5349
sem atualizar o cliente. Também é possível hospedar TURN em uma segunda VPS;
esse arranjo requer separar o serviço TURN do Compose desta máquina.

## Deploy automático com GitHub Actions

O workflow [Deploy VPS](../.github/workflows/deploy.yml) executa, em alterações
na `main` que afetam `server/`, `infra/` ou o próprio workflow:

1. Testes de salas, consentimento de relay e recuperação de deploy.
2. Validação dos dois arquivos Compose, construção da imagem e teste HTTPS/
   WebSocket com os containers reais e um certificado temporário no CI.
3. Acesso SSH com chave e fingerprint do servidor verificada.
4. Atualização para o commit exato que passou nos testes, construção na VPS,
   inicialização com healthcheck e verificação HTTPS de `/health`.
5. Recuperação do commit anterior se a atualização dos containers ou o healthcheck
   falhar. Uma falha de construção preserva os containers em execução.

Não precisa de registry Docker nem token GitHub na VPS: o repositório é público.
Os certificados e `infra/.env` ficam na VPS. No modo `signaling`, o workflow
cria/atualiza somente `SIGNAL_HOST` e `SIGNAL_IP` a partir das Variables, com
permissão 600 no arquivo, preservando os demais valores privados. Os certificados
não são sobrescritos nem enviados para o GitHub. Evite `docker compose config` sem `--quiet`, pois ele
exibe os valores resolvidos das variáveis, inclusive o segredo TURN.

### Preparação única da VPS

A instalação exige Git, Bash, `flock`, Python 3, curl, Docker Engine e o plugin
Docker Compose com suporte a `up --wait`. Escolha as instruções de instalação
correspondentes ao sistema operacional da VPS. O exemplo pressupõe Linux x64,
IPs públicos atribuídos diretamente, porta 443 livre e certificados já emitidos.

Crie um usuário de deploy e um diretório que ele possa atualizar. O usuário
precisa acessar Docker; essa permissão permite administrar a máquina. Use uma
chave SSH exclusiva para esta automação e instale sua chave pública em
`~/.ssh/authorized_keys` desse usuário.

Com as permissões preparadas, execute como usuário de deploy:

```sh
git clone https://github.com/LazaroLanderson/lazarus-share.git /opt/lazarus-share
cd /opt/lazarus-share
cp infra/.env.example infra/.env
chmod 600 infra/.env
mkdir -p infra/certs/signal infra/certs/turn
```

Para executar primeiro de forma manual, edite `infra/.env`. No deploy automático
`signaling`, o arquivo também pode ser gerado pelas Variables do GitHub.
Configure DNS/firewall e coloque os certificados válidos
nos diretórios indicados. Inclua `SIGNAL_HOST=salas.seudominio.com` para o teste
HTTPS do deploy. No modo completo, gere um segredo TURN aleatório com o comando
do exemplo e use o mesmo valor em sinalização e Coturn. Não use os IPs de exemplo.

Faça o primeiro deploy manual, na VPS, depois dessa preparação:

```sh
cd /opt/lazarus-share
# Uma VPS com um IP, somente salas/P2P:
bash infra/deploy.sh "$(git rev-parse HEAD)" /opt/lazarus-share signaling
# Ou, com os dois IPs e TURN preparados:
# bash infra/deploy.sh "$(git rev-parse HEAD)" /opt/lazarus-share full
```

A recuperação automática exige uma instalação anterior funcional com o mesmo
modo de Compose. Na primeira instalação, uma falha pode exigir correção manual;
não há versão anterior em funcionamento para restaurar. Mudanças de modo,
certificados, firewall ou `.env` não são desfeitas pelo rollback de código.

### Configurar o GitHub

No repositório, abra **Settings → Environments**, crie o ambiente `production`
e limite os deploys à branch `main`. Se desejar deploy completamente automático,
não configure aprovação manual nesse ambiente.

Em **Settings → Secrets and variables → Actions → Secrets**, configure os
secrets de repositório (os nomes abaixo correspondem às credenciais criadas):

| Secret | Conteúdo |
| --- | --- |
| `VPS_HOST` | IP público ou hostname da VPS acessível por SSH |
| `VPS_USER` | Usuário de deploy na VPS |
| `VPS_SSH_KEY` | Chave privada exclusiva do deploy, sem passphrase |
| `VPS_SSH_KNOWN_HOSTS` | Entrada SSH `known_hosts` da VPS verificada por um canal confiável |

`VPS_HOST` é o destino SSH do deploy. No modo `signaling`, configure
`SIGNAL_HOST` nas Variables para gerar o `.env` na VPS automaticamente. DNS e
certificados continuam sendo preparados na infraestrutura; os apps usam o domínio
público e não recebem os secrets do GitHub.
No modo `signaling`, `SIGNAL_IP` ausente ou vazio usa `0.0.0.0`, que permite
escutar nas interfaces locais sem tentar vincular um IP público que pode estar
atrás de NAT. `0.0.0.0` é endereço de escuta, não endereço para digitar no app
nem para usar no DNS. Ao copiar `.env.example`, substitua o IP de exemplo por
`0.0.0.0` nesse modo, ou pelo IP local realmente atribuído. Para o modo `full`,
configure explicitamente os dois IPs distintos.

Em **Variables**, configure:

| Variável | Valor |
| --- | --- |
| `SIGNAL_HOST` | Domínio real das salas, como `salas.seudominio.com`, sem `https://` nem `/ws`; obrigatório no deploy automático `signaling` |
| `SIGNAL_IP` | IP local de escuta; padrão `0.0.0.0` no modo `signaling` |
| `VPS_PORT` | Porta SSH; padrão `22` |
| `VPS_DEPLOY_PATH` | Checkout na VPS; padrão `/opt/lazarus-share` |
| `VPS_DEPLOY_MODE` | Padrão `signaling` para um IP; `full` para salas + TURN |
| `VPS_DEPLOY_ENABLED` | `true`, somente depois do primeiro deploy manual bem-sucedido |

Os secrets também podem ser definidos no ambiente `production`; secrets do
repositório ficam disponíveis ao job, desde que não sejam sobrescritos por
secrets desse ambiente. Para compatibilidade, o workflow ainda aceita host e
usuário em Variables e a chave com o nome antigo `VPS_SSH_PRIVATE_KEY`, mas
prioriza os secrets `VPS_HOST`, `VPS_USER` e `VPS_SSH_KEY`.

Obtenha o fingerprint SSH pela console da VPS ou pelo provedor e compare com a
chave recebida antes de salvar `known_hosts`. Para portas diferentes de 22, a
entrada usa `[hostname]:porta`. A automação não aceita hosts desconhecidos
nem desativa a verificação SSH. Não compartilhe chaves privadas em chats/issues.

Após habilitar, um push nos caminhos relevantes inicia o deploy. Para executar
sem mudar código, abra **Actions → Deploy VPS → Run workflow** e selecione `main`.
Sem `VPS_DEPLOY_ENABLED=true`, os testes são executados e o acesso à VPS é pulado.
Somente uma implantação roda por vez; uma nova execução não interrompe a anterior.

### Operação e recuperação

Uma atualização recria os containers: salas em memória e allocations de relay
em uso são encerradas. Avise os usuários ou faça o deploy fora das sessões.
Não há atualização sem interrupção nesta versão.

O deploy salva `.deploy/current` e `.deploy/previous` na VPS. Para recuperar uma
versão manualmente, use o SHA desejado no mesmo script:

```sh
bash infra/deploy.sh SHA_DO_COMMIT /opt/lazarus-share signaling
```

A automação verifica a saúde HTTP da sinalização e o HTTPS externo. Isso não
valida um relay TURN: após preparar a VPS, teste P2P e TURN UDP/TCP/TLS entre
redes diferentes, inclusive recusa de relay e dupla autorização.

A emissão/renovação TLS continua sob responsabilidade da infraestrutura. Se
usar Certbot ou equivalente, copie os certificados renovados para os diretórios
montados e recrie os containers que os utilizam; automatize esse procedimento
no hook de renovação da ferramenta. Nenhum deploy solicita certificado nem
altera DNS ou firewall automaticamente.
