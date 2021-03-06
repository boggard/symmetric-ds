package org.jumpmind.symmetric.io;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

import org.apache.commons.lang3.StringUtils;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.hbase.HBaseConfiguration;
import org.apache.hadoop.hbase.TableName;
import org.apache.hadoop.hbase.client.Connection;
import org.apache.hadoop.hbase.client.ConnectionFactory;
import org.apache.hadoop.hbase.client.Delete;
import org.apache.hadoop.hbase.client.Put;
import org.apache.hadoop.hbase.client.Table;
import org.jumpmind.db.model.Column;
import org.jumpmind.symmetric.io.data.CsvData;
import org.jumpmind.symmetric.io.data.writer.AbstractDatabaseWriter;

public class HbaseDatabaseWriter extends AbstractDatabaseWriter {

    private Configuration config;
    private Connection connection;
    private Table table; 
    private String hbaseSiteXmlPath;
    
    public HbaseDatabaseWriter(String hbaseSiteXmlPath) {
        this.hbaseSiteXmlPath = hbaseSiteXmlPath;
    }
    
    protected void setup() {
        try {
            if (config == null) {
                config = HBaseConfiguration.create();
                config.addResource(new Path(this.hbaseSiteXmlPath));
            }
            
            if (connection == null) {
                log.debug("Establishing connection to HBase");
                connection = ConnectionFactory.createConnection(config);
            }
            if (table == null) {
                log.debug("Connected to HBase, now looking up table " + this.targetTable.getName());
                table = connection.getTable(TableName.valueOf(this.targetTable.getName()));
            }
        } catch (IOException e) {
            log.error("Unable to connect to HBase ", e);
        }
    }
    
    protected LoadStatus put(CsvData data) {
        try {
            setup();
            Put put = new Put(data.getPkData(this.targetTable)[0].getBytes());
            
            String[] values = data.getParsedData(CsvData.ROW_DATA);
            Column[] columns = sourceTable.getColumns();
            
            List<Put> putList = new ArrayList<Put>();
            
            for (int i = 0; i < columns.length; i++) {
                if (columns[i].getName().contains(":")) {
                    log.debug("Preparing put statement into Hbase.");
                    String[] split = columns[i].getName().split(":");
                    byte[] columnFamily = split[0].getBytes();
                    byte[] columnName = split[1].getBytes();
                    
                    byte[] value = StringUtils.isEmpty(values[i]) ? new byte[0] : values[i].getBytes();
                    put.addColumn(columnFamily, columnName, value);
                    putList.add(put);
                }
            }
            
            log.debug("Put list for HBase complete with a size of " + putList.size());
            table.put(putList);
            log.debug("Put rows into HBase now closing connection");
            table.close();
        } catch (IOException e) {
            log.error("Unable to load data into HBase ", e);
            throw new RuntimeException(e);
        }

        return LoadStatus.SUCCESS;
    }
    
    @Override
    protected LoadStatus insert(CsvData data) {
        return put(data);
    }

    @Override
    protected LoadStatus delete(CsvData data, boolean useConflictDetection) {
        setup();
        
        String[] pkData = data.getParsedData(CsvData.PK_DATA);
        if (pkData != null && pkData.length == 1) {
            Delete delete = new Delete(pkData[0].getBytes());
            try {
                table.delete(delete);
            } catch (IOException e) {
                log.error("Unable to delete data for table " + this.targetTable.getName() + 
                        ", for primary key " + pkData[0]);
                throw new RuntimeException(e);
            }
        }
        return LoadStatus.SUCCESS;
            
    }

    @Override
    protected LoadStatus update(CsvData data, boolean applyChangesOnly, boolean useConflictDetection) {
        return put(data);
    }

    @Override
    protected boolean create(CsvData data) {
        return false;
    }

    @Override
    protected boolean sql(CsvData data) {
        return false;
    }

    @Override
    protected void logFailureDetails(Throwable e, CsvData data, boolean logLastDmlDetails) {
    }

    
}
