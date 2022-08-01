use market;
#call mrk_statistic_update( 465119069, 0, 19200, 13.640000, 19124, 176.650000, 18939, 0.000000 )

DROP PROCEDURE IF EXISTS mrk_statistic_update;

DELIMITER $$
CREATE PROCEDURE mrk_statistic_update( _contract_id int unsigned, _day_count smallint, _last_update smallint, _low float, _low_day smallInt, _high float, _high_day smallint, _average float )
begin
	declare _low_statistics_id tinyint;
	declare _low_day_stat tinyint;
	declare _count int;
	DECLARE EXIT HANDLER FOR SQLEXCEPTION
   BEGIN
		ROLLBACK;
		RESIGNAL;
	END;
	START TRANSACTION;
	select if(_day_count=0, 1, 3) into _low_statistics_id; select if(_day_count=0, -1, -3) into _low_day_stat;
	select count(*) into _count from mrk_statistic_values where contract_id=_contract_id and statistics_id=_low_statistics_id;
	if( _count=0 ) then
		insert into mrk_statistic_values values( _contract_id, _low_statistics_id, _low );
		insert into mrk_statistic_values values( _contract_id, _low_day_stat, _low_day );
		insert into mrk_statistic_values values( _contract_id, _low_statistics_id+1, _high );
		insert into mrk_statistic_values values( _contract_id, _low_day_stat-1, _high_day );
		if( _day_count!=0 ) then
			insert into mrk_statistic_values values( _contract_id, 0, _last_update );
			insert into mrk_statistic_values values( _contract_id, 5, _average );
		end if;
	else
		update mrk_statistic_values set value=_low where contract_id=_contract_id and statistics_id=_low_statistics_id;
		update mrk_statistic_values set value=_low_day where contract_id=_contract_id and statistics_id=_low_day_stat;
		update mrk_statistic_values set value=_high where contract_id=_contract_id and statistics_id=_low_statistics_id+1;
		update mrk_statistic_values set value=_high_day where contract_id=_contract_id and statistics_id=_low_day_stat-1;
		if( _day_count!=0 ) then
			update mrk_statistic_values set value=_last_update where contract_id=_contract_id and statistics_id=0;
			update mrk_statistic_values set value=_average where contract_id=_contract_id and statistics_id=5;
		end if;
	end if;
	commit;
end
