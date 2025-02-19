## Script mysqlxtest_macro_dir.inc
#


--perl
use File::Basename;
use File::Spec::Functions;
my $mysql_test_file = $ENV{MYSQLTEST_FILE} or die "Need MYSQLTEST_FILE";
my $basedir = dirname($mysql_test_file);

my $tmpdir = $ENV{MYSQL_TMP_DIR} or die "Need $MYSQL_TMP_DIR";
my $macro_file = catdir($basedir, 'macros');
open(FILE, ">", "$tmpdir/mysqlxtest_macro_dir.tmp.inc") or die;
print FILE "--let \$MYSQLXTEST_MACRO_DIR= $macro_file\n";
close(FILE);
EOF

if (!$MYSQLXTEST_HISTORY_PROTOCOL)
{
  --let $MYSQLXTEST_HISTORY_PROTOCOL = true
}

--source $MYSQL_TMP_DIR/mysqlxtest_macro_dir.tmp.inc
--remove_file $MYSQL_TMP_DIR/mysqlxtest_macro_dir.tmp.inc
--let $MYSQLXTEST = $MYSQLXTEST --import="$MYSQLXTEST_MACRO_DIR" --history-protocol $MYSQLXTEST_HISTORY_PROTOCOL $MYSQLXTEST_EXTRA_ARGS
