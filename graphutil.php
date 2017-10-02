<?php

define('STATIC_DIR', '/var/lib/gpio-isr');
define('VOLATILE_DIR', '/var/run/gpio-isr');
define('WWW_DIR', VOLATILE_DIR.'/html');

@mkdir(WWW_DIR);

function rrd_create ($rrdfile, $step, $dsname) {
	$heartbeat = $step * 2;
	print "creating rrdfile: $rrdfile\n";
	$dps_per_hour = 3600 / $step;
	$dps_per_day = 86400 / $step;
	$keep_exact_for = 86400 * 30 / $step; // 30 days
	$keep_hourly_for = 365 * 24;          // 1 year
	$keep_daily_for = 365 * 10;           // 10 years
	system("/usr/bin/rrdtool create $rrdfile --step $step --start ".(time()-$step+1)." \
	       DS:$dsname:GAUGE:$heartbeat:0:U \
	       RRA:AVERAGE:0.5:1:$keep_exact_for \
	       RRA:AVERAGE:0.5:$dps_per_hour:$keep_hourly_for \
	       RRA:AVERAGE:0.5:$dps_per_day:$keep_daily_for \
	       RRA:MAX:0.5:1:$keep_exact_for \
	       RRA:MAX:0.5:$dps_per_hour:$keep_hourly_for \
	       RRA:MAX:0.5:$dps_per_day:$keep_daily_for \
	       ");
}

$aggs = array();
$aggs['lastday'] = array('start' => -86400, 'title' => 'letzter Tag', 'interval' => 300);
$aggs['last3h'] = array('start' => -3600*3, 'title' => 'letzte 3 Stunden', 'interval' => 60);
$aggs['lastweek'] = array('start' => -86400*7, 'title' => 'letzte Woche', 'interval' => 3600);

for ($i = 0; $i < 64; $i++) {
	if (!file_exists(STATIC_DIR.'/pin'.$i.'.graph-dsname')) continue;
	$rrdfile = STATIC_DIR.'/pin'.$i.'.rrd';
	$dsname = trim(file_get_contents(STATIC_DIR.'/pin'.$i.'.graph-dsname'));
	if (!file_exists($rrdfile)) rrd_create($rrdfile, 60, $dsname);
	$lastPeriod = @file_get_contents(VOLATILE_DIR.'/pin'.$i.'.lastPeriod');
	if (!$lastPeriod) continue;
	system("/usr/bin/rrdtool update $rrdfile N:".intval(trim($lastPeriod)));
	$dividend = intval(file_get_contents(STATIC_DIR.'/pin'.$i.'.graph-dividend'));

	foreach ($aggs as $k => $params) {
		if (time() % $params['interval'] >= 60) continue;
		$start = time() + $params['start'];
		$title = trim(@file_get_contents(STATIC_DIR.'/pin'.$i.'.name')).' ['.$params['title'].']';
		$unit = trim(@file_get_contents(STATIC_DIR.'/pin'.$i.'.graph-unit'));
		$vlabel = trim(@file_get_contents(STATIC_DIR.'/pin'.$i.'.graph-y-axis'));
		$outfile = WWW_DIR.'/pin'.$i.'_'.$k.'.png';
		$h = 400;
		$w = 1200;
		$v = array("#003366", "LINE1", $dsname);
		$cmd = "/usr/bin/rrdtool graph $outfile -D --imgformat=PNG  --start=$start --title '$title' --vertical-label='$vlabel' --height=$h --width=$w --alt-autoscale-max --lower-limit=0 --slope-mode  --font AXIS:8:  --font LEGEND:9:  --font UNIT:8: ";
		$cmd.=" DEF:xa_$dsname=$rrdfile:$dsname:AVERAGE CDEF:a_$dsname=$dividend,xa_$dsname,/ $v[1]:a_$dsname$v[0]:\"$v[2]\"";
		$cmd.=" DEF:xb_$dsname=$rrdfile:$dsname:MAX CDEF:b_$dsname=$dividend,xb_$dsname,/";
		$cmd.=" GPRINT:a_$dsname:LAST:\"\tCur\: %5.1lf%s\"$unit GPRINT:a_$dsname:MIN:\"  Min\: %5.1lf%s\"$unit GPRINT:b_$dsname:MAX:\" Max\: %5.1lf%s$unit\\n\"";
		passthru($cmd.' >/dev/null');
	}
}

?>
