delete from `gameobject` where `map` = 617;
insert into `gameobject` values
-- buffs
('200060','184663','617','1','1','1291.7','813.424','7.11472','4.64562','0','0','0.730314','-0.683111','-120','100','1'),
('200061','184664','617','1','1','1291.7','768.911','7.11472','1.55194','0','0','0.700409','0.713742','-120','100','1'),
-- doors
('200062','192642','617','1','1','1350.95','817.2','21.4096','3.15','0','0','0.99627','0.0862864','86400','100','1'),
('200063','192643','617','1','1','1232.65','764.913','21.4729','6.3','0','0','0.0310211','-0.999519','86400','100','1'),
-- waterfall
('200064','194395','617','1','1', 1291.6, 791.05, 7.11, 3.22012 ,'0','0', 0.999229, -0.0392542 ,'-120','100','1'),
('200065','191877','617','1','1', 1291.6, 791.05, 7.11, 3.22012 ,'0','0', 0.999229, -0.0392542 ,'-120','100','1');


delete from `gameobject_battleground` where `guid` in (200060,200061,200062,200063,200064,200065);
insert into `gameobject_battleground` values
-- buffs
('200060','252','0'),
('200061','252','0'),
-- doors
('200062','254','0'),
('200063','254','0'),
-- waterfall
('200064','250','0'),
('200065','250','0');


delete from `battleground_events` where `map` = 617;
insert into `battleground_events` values
('617','252','0','buffs'),
('617','254','0','doors'),
('617','250','0','waterfall');

-- doors
update `gameobject_template` set `faction` = 114, `flags` = 32, `size` = 1.5 where `entry` in (192642,192643);
