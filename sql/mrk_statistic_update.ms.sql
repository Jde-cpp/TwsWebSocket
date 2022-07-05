--drop proc mrk_statistic_update;
CREATE PROCEDURE mrk_statistic_update( @contract_id int, @day_count smallint, @last_update smallint, @low float, @low_day smallInt, @high float, @high_day smallint, @average float )
as
	set nocount on
	declare @low_statistics_id tinyint = iif(@day_count=0, 1, 3), @low_day_stat tinyint=iif(@day_count=0, 255, 253)
	begin transaction
	if( 0=(select count(*) from mrk_statistic_values where contract_id=@contract_id and statistics_id=@low_statistics_id) )
	begin
		insert into mrk_statistic_values values( @contract_id, @low_statistics_id, @low );
		insert into mrk_statistic_values values( @contract_id, @low_day_stat, @low_day );
		insert into mrk_statistic_values values( @contract_id, @low_statistics_id+1, @high );
		insert into mrk_statistic_values values( @contract_id, @low_day_stat-1, @high_day );
		if( @day_count!=0 )
		begin
			insert into mrk_statistic_values values( @contract_id, 0, @last_update );
			insert into mrk_statistic_values values( @contract_id, 5, @average );
		end
	end
	else
	begin
		update mrk_statistic_values set value=@low where contract_id=@contract_id and statistics_id=@low_statistics_id;
		update mrk_statistic_values set value=@low_day where contract_id=@contract_id and statistics_id=@low_day_stat;
		update mrk_statistic_values set value=@high where contract_id=@contract_id and statistics_id=@low_statistics_id+1;
		update mrk_statistic_values set value=@high_day where contract_id=@contract_id and statistics_id=@low_day_stat-1;
		if( @day_count!=0 )
		begin
			update mrk_statistic_values set value=@last_update where contract_id=@contract_id and statistics_id=0;
			update mrk_statistic_values set value=@average where contract_id=@contract_id and statistics_id=5;
		end
	end
	commit