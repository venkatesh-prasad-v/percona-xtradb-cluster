
--write_file $MYSQL_TMP_DIR/resultset.xpl
-->import assert_messages.macro
-->import assert_notices.macro

-->varlet %ID% 1
-->macro Prepare_and_cursor_open_wo_res	%OTHER_OPTIONS%	%SQL%
-->varinc %ID% 1
-->noquery_result
-->quiet
-->echo [PREPARE - %SQL%]
Mysqlx.Prepare.Prepare {
  stmt_id: %ID%
  stmt: {
    type: STMT
    stmt_execute: {
      stmt: "%SQL%"
    }
  }
}
-->recvok
-->echo [EXECUTE]
Mysqlx.Cursor.Open {
  cursor_id: %ID%
  stmt {
    type: PREPARE_EXECUTE
    prepare_execute {
      stmt_id: %ID%
    }
  }
  %OTHER_OPTIONS%
}
-->query_result
-->noquiet
-->endmacro


-->macro Cursor_fetch_wo_res	%OTHER_OPTIONS%
-->noquery_result
-->quiet
-->echo [FETCH]
Mysqlx.Cursor.Fetch {
  cursor_id: %ID%
  %OTHER_OPTIONS%
}
-->query_result
-->noquiet
-->endmacro

##
## Purpose of this test is:
#
# * validating that resultsets are in align to the UML flow:
#
#   ...
#   loop has more resultsets or not at end of fetch
#     group resultset
#       loop has more columns
#         server --> client: ColumnMetaData
#       end
#       loop has more rows
#         server --> client: Row
#       end
#     end
#     alt has more resultsets
#       server --> client: FetchDoneMoreResultsets
#     end
#   end
#   loop has more OUT-paramsets or not at end of fetch
#     server --> client: FetchDoneMoreOutParams
#     group resultset
#       loop has more columns
#         server --> client: ColumnMetaData
#       end
#       loop has more rows
#         server --> client: Row
#       end
#     end
#   end
#   alt at end of all resultsets
#     server --> client: FetchDone
#   else cursor is opened
#     server --> client: FetchSuspended
#   end
#   ...
#
# * expecting following notices for SQL prepared-statement:
#
# |Notice\Stmt            |Select |Insert |Update |Delete |
# |-----------------------|-------|-------|-------|-------|
# |ROWS_AFFECTED          |X(0)   |X      |X      |X      |
# |PRODUCED_MESSAGE       |       |       |X      |       |
# |GENERATED_INSERT_ID    |       |X(PK)  |       |       |
# |GENERATED_DOCUMENT_IDS |       |       |       |       |
#
# * cover fetching responses of static SQL and dynamic SQL:
#
#   Resultsets generated by dynamic SQL (for example procedures) can't
#   be buffered by cursors and are executed right away.
#   Because of this issue, Mysqlx.Cursor.Open may return a resultset instead
#   of Mysqlx.Ok.
#
-->echo
-->echo
-->echo ## I. Validate non dynamic SQL statement where Cursor.Fetch is not needed
-->echo #
-->echo # 1. Assert empty resultset, dont get any rows at Cursor.Open.
-->echo # 2. Assert resultset that has less rows than requested by Cursor.Open.
-->echo
-->echo ## II. Validate non dynamic SQL statement where Cursor.Fetch is needed to get all data
-->echo #
-->echo # 1. Assert empty resultset, still try to get at last one row at open
-->echo # 2. Assert resultset that has same amount of rows as requested
-->echo # 3. Assert resultset that has rows grater by one than requested
-->echo # 4. Assert resultset that has two times the amount of rows as requested
-->echo # 5. Assert resultset by fetching the resultset one by one row
-->echo # 6. Assert resultset by fetching the resultset and warnings generated by it
-->echo
-->echo ## III. Validate dynamic SQL statement
-->echo #
-->echo # 1. Assert that cursor open, fetches whole resultset
-->echo #   a. fetch_rows in Cursor.Open is not set
-->echo #   b. fetch_rows in Cursor.Open is set (value doesn't matter).
-->echo # 2. Assert that cursor open, fetches multiple resultsets
-->echo # 3. Assert that cursor open, fetches multiple resultsets with warnings
-->echo
-->echo ## IV. Validate notices
-->echo #
-->echo # 1. Insert stmt
-->echo #   a. Table with primary key (expect notice GENERATED_INSERT_ID)
-->echo #   b. Table without primary key (must not be generate GENERATED_INSERT_ID)
-->echo # 2. Delete stmt
-->echo # 3. Update stmt
-->echo


-->echo
-->echo #
-->echo # I.1
callmacro Prepare_and_cursor_open_wo_res
	fetch_rows: 1
	SELECT * FROM xtable LIMIT 0;
callmacro Assert_metadata	BYTES	"phrase";
callmacro Assert_metadata	SINT	"prio";
callmacro Assert_message	Mysqlx.Resultset.FetchDone;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:0;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # I.2
callmacro Prepare_and_cursor_open_wo_res
	fetch_rows: 1000
	SELECT * FROM xtable;
callmacro Assert_metadata	BYTES	"phrase";
callmacro Assert_metadata	SINT	"prio";
callmacro Assert_rows	5;
callmacro Assert_message	Mysqlx.Resultset.FetchDone;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:0;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # II.1
callmacro Prepare_and_cursor_open_wo_res
		SELECT * FROM xtable LIMIT 0;
callmacro Assert_metadata	BYTES	"phrase";
callmacro Assert_metadata	SINT	"prio";
callmacro Assert_message	Mysqlx.Resultset.FetchSuspended;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;

callmacro Cursor_fetch_wo_res	fetch_rows: 1000;
callmacro Assert_message	Mysqlx.Resultset.FetchDone;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:0;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # II.2
callmacro Prepare_and_cursor_open_wo_res
	fetch_rows: 5
	SELECT * FROM xtable LIMIT 5;
callmacro Assert_metadata	BYTES	"phrase";
callmacro Assert_metadata	SINT	"prio";
callmacro Assert_rows	5;
callmacro Assert_message	Mysqlx.Resultset.FetchSuspended;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;

callmacro Cursor_fetch_wo_res	fetch_rows: 1000;
callmacro Assert_message	Mysqlx.Resultset.FetchDone;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:0;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # II.3
callmacro Prepare_and_cursor_open_wo_res
	fetch_rows: 4
	SELECT * FROM xtable LIMIT 5;
callmacro Assert_metadata	BYTES	"phrase";
callmacro Assert_metadata	SINT	"prio";
callmacro Assert_rows	4;
callmacro Assert_message	Mysqlx.Resultset.FetchSuspended;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;

callmacro Cursor_fetch_wo_res	fetch_rows: 1000;
callmacro Assert_rows	1;
callmacro Assert_message	Mysqlx.Resultset.FetchDone;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:0;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # II.4
callmacro Prepare_and_cursor_open_wo_res
	fetch_rows: 2
	SELECT * FROM xtable LIMIT 4;
callmacro Assert_metadata	BYTES	"phrase";
callmacro Assert_metadata	SINT	"prio";
callmacro Assert_rows	2;
callmacro Assert_message	Mysqlx.Resultset.FetchSuspended;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;

callmacro Cursor_fetch_wo_res	fetch_rows: 1000;
callmacro Assert_rows	2;
callmacro Assert_message	Mysqlx.Resultset.FetchDone;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:0;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # II.5
callmacro Prepare_and_cursor_open_wo_res
	fetch_rows: 1
	SELECT * FROM xtable LIMIT 3;
callmacro Assert_metadata	BYTES	"phrase";
callmacro Assert_metadata	SINT	"prio";
callmacro Assert_rows	1;
callmacro Assert_message	Mysqlx.Resultset.FetchSuspended;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;

callmacro Cursor_fetch_wo_res	fetch_rows: 1;
callmacro Assert_rows	1;
callmacro Assert_message	Mysqlx.Resultset.FetchSuspended;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;

callmacro Cursor_fetch_wo_res	fetch_rows: 1;
callmacro Assert_rows	1;
callmacro Assert_message	Mysqlx.Resultset.FetchSuspended;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;

callmacro Cursor_fetch_wo_res	fetch_rows: 1;
callmacro Assert_message	Mysqlx.Resultset.FetchDone;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:0;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # II.6
callmacro Prepare_and_cursor_open_wo_res
	
	SELECT 1/prio as col1 FROM xtable;
-->callmacro Assert_metadata	DECIMAL	"col1"
callmacro Assert_message	Mysqlx.Resultset.FetchSuspended;
## ER_DIVISION_BY_ZERO == 1365
callmacro Assert_notice_warning	WARNING	1365;
callmacro Assert_notice_warning	WARNING	1365;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;

callmacro Cursor_fetch_wo_res	fetch_rows: 2;
callmacro Assert_rows	2;
callmacro Assert_message	Mysqlx.Resultset.FetchSuspended;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;

callmacro Cursor_fetch_wo_res	fetch_rows: 4;
callmacro Assert_rows	3;
callmacro Assert_message	Mysqlx.Resultset.FetchDone;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:0;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # III.1.a
callmacro Prepare_and_cursor_open_wo_res
	
	CALL one_resultsets();
callmacro Assert_metadata	BYTES	"phrase";
callmacro Assert_rows	3;
callmacro Assert_message	Mysqlx.Resultset.FetchDoneMoreResultsets;
callmacro Assert_message	Mysqlx.Resultset.FetchDone;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:0;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # III.1.b
callmacro Prepare_and_cursor_open_wo_res
	fetch_rows: 1
	CALL one_resultsets();
callmacro Assert_metadata	BYTES	"phrase";
callmacro Assert_rows	3;
callmacro Assert_message	Mysqlx.Resultset.FetchDoneMoreResultsets;
callmacro Assert_message	Mysqlx.Resultset.FetchDone;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:0;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # III.2
callmacro Prepare_and_cursor_open_wo_res
	
	CALL two_resultsets();
callmacro Assert_metadata	BYTES	"phrase";
callmacro Assert_rows	3;
callmacro Assert_message	Mysqlx.Resultset.FetchDoneMoreResultsets;
callmacro Assert_metadata	SINT	"prio";
callmacro Assert_rows	2;
callmacro Assert_message	Mysqlx.Resultset.FetchDoneMoreResultsets;
callmacro Assert_message	Mysqlx.Resultset.FetchDone;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:0;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # III.3
callmacro Prepare_and_cursor_open_wo_res
	
	CALL two_resultsets_with_errors;
-->callmacro Assert_metadata	DECIMAL	"col1"
-->callmacro Assert_rows	5
-->callmacro Assert_message	Mysqlx.Resultset.FetchDoneMoreResultsets
-->callmacro Assert_metadata	DECIMAL	"col1"
-->callmacro Assert_rows	5
-->callmacro Assert_message	Mysqlx.Resultset.FetchDoneMoreResultsets
-->callmacro Assert_message	Mysqlx.Resultset.FetchDone
## ER_DIVISION_BY_ZERO == 1365
-->callmacro Assert_notice_warning	WARNING	1365
-->callmacro Assert_notice_warning	WARNING	1365
-->callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:0
-->callmacro Assert_message Mysqlx.Sql.StmtExecuteOk


-->echo
-->echo #
-->echo # IV.1.a
callmacro Prepare_and_cursor_open_wo_res
	
	INSERT INTO xtest.xtable_with_pk(description) VALUE('MyCustomRow');
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:1;
callmacro Assert_notice_session_state	GENERATED_INSERT_ID	v_unsigned_int: 3;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # IV.1.b
callmacro Prepare_and_cursor_open_wo_res
	
	INSERT INTO xtest.xtable(phrase, prio) VALUE('MyCustomRow', 100);
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:1;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # IV.2
callmacro Prepare_and_cursor_open_wo_res
	
	DELETE FROM xtest.xtable WHERE prio=100;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:1;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;


-->echo
-->echo #
-->echo # IV.3
callmacro Prepare_and_cursor_open_wo_res
	
	UPDATE xtest.xtable SET phrase='New';
callmacro Assert_notice_session_state	PRODUCED_MESSAGE	type: V_STRING;
callmacro Assert_notice_session_state	ROWS_AFFECTED	v_unsigned_int:5;
callmacro Assert_message Mysqlx.Sql.StmtExecuteOk;

EOF


CREATE SCHEMA IF NOT EXISTS xtest;
USE xtest;

DELIMITER //;

# Data are similar to sample_tables.inc,
# still prio in decremented by one
CREATE PROCEDURE recreate_tables()
BEGIN
  CREATE TABLE IF NOT EXISTS xtable (phrase VARCHAR(30), prio INTEGER);
  TRUNCATE TABLE xtable;
  INSERT INTO xtable (phrase, prio) VALUES ('Alice has a cat', 0);
  INSERT INTO xtable (phrase, prio) VALUES ('Bob has a dog 1', 1);
  INSERT INTO xtable (phrase, prio) VALUES ('Peppa has a teddy', 0);
  INSERT INTO xtable (phrase, prio) VALUES ('Bob has a dog 2', 2);
  INSERT INTO xtable (phrase, prio) VALUES ('Row to delete', 3);
  
  CREATE TABLE IF NOT EXISTS xtable_with_pk (p_id INTEGER AUTO_INCREMENT PRIMARY KEY, description varchar(40));
  TRUNCATE TABLE xtable_with_pk;
  INSERT INTO xtable_with_pk (description) VALUES ('MyRow');
END //

CREATE PROCEDURE one_resultsets()
BEGIN
  SELECT phrase FROM xtable limit 3;
END //

CREATE PROCEDURE two_resultsets()
BEGIN
  SELECT phrase FROM xtable limit 3;
  SELECT prio FROM xtable limit 2;
END //

CREATE PROCEDURE two_resultsets_with_errors()
BEGIN
  SELECT 1/prio as col1 FROM xtable;
  SELECT 1/prio as col1 FROM xtable;
END //

DELIMITER ;//


