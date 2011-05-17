#!/usr/bin/php
<?php

$o = `ipcs`;
$me = trim(`whoami`);

$sections = preg_split('/------[\sa-z]+--------/i', $o);

if(count($sections) != 4)
        die( "Unable to parse result from ipcs\n");

list($blank, $txt_shmem, $txt_sems, $txt_queues)=$sections;

$shmem = array();
$sems = array();

preg_match_all('/0x[a-z0-9]{8}[\s]+([\d]+)[\s]+([a-z0-9]+)/is', $txt_shmem, $matches, PREG_SET_ORDER);
foreach($matches as $match) {
        if($me != $match[2])
                continue;
        $shmem[] = '-m ' . $match[1];
}

preg_match_all('/0x[a-z0-9]{8}[\s]+([\d]+)[\s]+([a-z0-9]+)/is', $txt_sems, $matches, PREG_SET_ORDER);
foreach($matches as $match) {
        if($me != $match[2])
                continue;
        $sems[] = '-s ' . $match[1];
}


$cmd = "ipcrm " . implode(' ', $shmem) . " "  . implode(' ', $sems);

echo "Calling:\n$cmd\n";

`$cmd`;
