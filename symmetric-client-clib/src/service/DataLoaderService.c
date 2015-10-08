/**
 * Licensed to JumpMind Inc under one or more contributor
 * license agreements.  See the NOTICE file distributed
 * with this work for additional information regarding
 * copyright ownership.  JumpMind Inc licenses this file
 * to you under the GNU General Public License, version 3.0 (GPLv3)
 * (the "License"); you may not use this file except in compliance
 * with the License.
 *
 * You should have received a copy of the GNU General Public License,
 * version 3.0 (GPLv3) along with this library; if not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include "service/DataLoaderService.h"
#include "common/Log.h"

static void SymDataLoaderService_sendAck(SymDataLoaderService *this, SymNode *remote, SymNode *local, SymNodeSecurity *localSecurity,
        SymList *incomingBatches) {
    int sendAck = -1;
    int numberOfStatusSendRetries = this->parameterService->getInt(this->parameterService,
            SYM_PARAMETER_DATA_LOADER_NUM_OF_ACK_RETRIES, 5);
    char *registrationUrl = this->parameterService->getRegistrationUrl(this->parameterService);
    int sleepSeconds = this->parameterService->getInt(this->parameterService, SYM_PARAMETER_DATA_LOADER_TIME_BETWEEN_ACK_RETRIES, 5);

    int i = 0;
    for (; i < numberOfStatusSendRetries && sendAck != SYM_TRANSPORT_OK; i++) {
        sendAck = this->transportManager->sendAcknowledgement(this->transportManager,
                    remote, incomingBatches, local, localSecurity->nodePassword, registrationUrl);
        if (sendAck != SYM_TRANSPORT_OK) {
            SymLog_warn("Ack was not sent successfully on try number %d.", i + 1);
            if (i < numberOfStatusSendRetries - 1) {
                sleep(sleepSeconds);
            }
        }
    }
}

static SymList * SymDataLoaderService_loadDataFromTransport(SymDataLoaderService *this, SymNode *remote, SymIncomingTransport *transport, int *error) {
    // TODO:
    SymDataWriter *writer = (SymDataWriter *) SymDefaultDatabaseWriter_new(NULL, this->incomingBatchService, this->platform, this->dialect);
    SymDataReader *reader = (SymDataReader *) SymProtocolDataReader_new(NULL, remote->nodeId, writer);

    long rc = transport->process(transport, reader);
    SymLog_debug("Transport rc = %ld" , rc);

    SymList *batchesProcessed = writer->batchesProcessed;
    reader->destroy(reader);
    writer->destroy(writer);
    return batchesProcessed;
}

void SymDataLoaderService_loadDataFromRegistration(SymDataLoaderService *this, SymRemoteNodeStatus *status) {
    SymNode *local = SymNode_new(NULL);
    local->nodeGroupId = this->parameterService->getNodeGroupId(this->parameterService);
    local->externalId = this->parameterService->getExternalId(this->parameterService);
    local->databaseType = this->platform->name;
    local->databaseVersion = this->platform->version;
    local->syncUrl = this->parameterService->getSyncUrl(this->parameterService);
    local->schemaVersion = this->parameterService->getString(this->parameterService, SYM_PARAMETER_SCHEMA_VERSION, "");

    char *registrationUrl = this->parameterService->getRegistrationUrl(this->parameterService);
    SymIncomingTransport *transport = this->transportManager->getRegisterTransport(this->transportManager, local, registrationUrl);
    SymLog_info("Using registration URL of %s\n", transport->getUrl(transport));

    SymNode *remote = SymNode_new(NULL);
    remote->syncUrl = registrationUrl;

    int error = 0;
    SymList *incomingBatches = SymDataLoaderService_loadDataFromTransport(this, remote, transport, &error);
    if (incomingBatches->size > 0) {
        status->updateIncomingStatus(status, incomingBatches);
        local->destroy(local);
        local = this->nodeService->findIdentity(this->nodeService);
        if (local != NULL) {
            SymNodeSecurity *localSecurity = this->nodeService->findNodeSecurity(this->nodeService, local->nodeId);
            SymDataLoaderService_sendAck(this, remote, local, localSecurity, incomingBatches);
            localSecurity->destroy(localSecurity);
        }
    }
    SymList_destroyAll(incomingBatches, (void *) SymIncomingBatch_destroy);

    local->destroy(local);
    remote->destroy(remote);
    transport->destroy(transport);
}

void SymDataLoaderService_loadDataFromPull(SymDataLoaderService *this, SymNode *remote, SymRemoteNodeStatus *status) {
    SymNode *local = this->nodeService->findIdentity(this->nodeService);
    if (local == NULL) {
        this->loadDataFromRegistration(this, status);
    } else {
        SymNodeSecurity *localSecurity = this->nodeService->findNodeSecurity(this->nodeService, local->nodeId);
        char *registrationUrl = this->parameterService->getRegistrationUrl(this->parameterService);
        SymIncomingTransport *transport = this->transportManager->getPullTransport(this->transportManager, remote, local,
                    localSecurity->nodePassword, NULL, registrationUrl);

        int error = 0;
        SymList *incomingBatches = SymDataLoaderService_loadDataFromTransport(this, remote, transport, &error);
        if (incomingBatches->size > 0) {
            status->updateIncomingStatus(status, incomingBatches);
            SymDataLoaderService_sendAck(this, remote, local, localSecurity, incomingBatches);
        }
        SymList_destroyAll(incomingBatches, (void *) SymIncomingBatch_destroy);

        if (error == 1) {
        	SymLog_warn("Node information missing on the server.  Attempting to re-register.");
            this->loadDataFromRegistration(this, status);
        }

        transport->destroy(transport);
        localSecurity->destroy(localSecurity);
        local->destroy(local);
    }
}

void SymDataLoaderService_destroy(SymDataLoaderService *this) {
    free(this);
}

SymDataLoaderService * SymDataLoaderService_new(SymDataLoaderService *this, SymParameterService *parameterService, SymNodeService *nodeService,
        SymTransportManager *transportManager, SymDatabasePlatform *platform, SymDialect *dialect, SymIncomingBatchService *incomingBatchService) {
    if (this == NULL) {
        this = (SymDataLoaderService *) calloc(1, sizeof(SymDataLoaderService));
    }
    this->parameterService = parameterService;
    this->nodeService = nodeService;
    this->transportManager = transportManager;
    this->platform = platform;
    this->dialect = dialect;
    this->incomingBatchService = incomingBatchService;

    this->loadDataFromPull = (void *) &SymDataLoaderService_loadDataFromPull;
    this->loadDataFromRegistration = (void *) &SymDataLoaderService_loadDataFromRegistration;
    this->destroy = (void *) &SymDataLoaderService_destroy;
    return this;
}

