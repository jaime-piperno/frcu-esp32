using Microsoft.EntityFrameworkCore.Infrastructure;
using Microsoft.EntityFrameworkCore.Migrations;
using RecuerdaMed.Api.Persistence;

#nullable disable

namespace RecuerdaMed.Api.Migrations;

[DbContext(typeof(AppDbContext))]
[Migration("20261107210000_StructuredDosageAndScheduleMessage")]
public sealed class StructuredDosageAndScheduleMessage : Migration
{
    protected override void Up(MigrationBuilder migrationBuilder)
    {
        migrationBuilder.AddColumn<decimal>(
            name: "DosageAmount", table: "medications", type: "numeric(10,3)", nullable: true);
        migrationBuilder.AddColumn<short>(
            name: "DosageUnit", table: "medications", type: "smallint", nullable: true);
        migrationBuilder.AddColumn<string>(
            name: "Message", table: "dose_schedules", type: "character varying(300)",
            maxLength: 300, nullable: true);

        // Convert only doses whose unit is explicit. The two legacy Benandryl
        // values were confirmed by the owner as 10 mg and 6 mg.
        migrationBuilder.Sql("""
            UPDATE medications
            SET "DosageAmount" = replace(substring(btrim("Dosage") FROM '^([0-9]+([.,][0-9]{1,3})?)'), ',', '.')::numeric,
                "DosageUnit" = CASE
                    WHEN btrim("Dosage") ~* '(mg|miligramos?)$'
                      OR (lower(btrim("Name")) = 'benandryl' AND btrim("Dosage") IN ('10', '6')) THEN 0
                    ELSE 1
                END
            WHERE btrim("Dosage") ~* '^[0-9]+([.,][0-9]{1,3})?[[:space:]]*(mg|miligramos?|pastillas?|tabletas?|unidad(es)?)$'
               OR (lower(btrim("Name")) = 'benandryl' AND btrim("Dosage") IN ('10', '6'));

            DO $$
            BEGIN
                IF EXISTS (SELECT 1 FROM medications WHERE "DosageAmount" IS NULL OR "DosageAmount" <= 0) THEN
                    RAISE EXCEPTION 'Cannot migrate medication doses with missing or ambiguous units. Edit legacy Dosage values before retrying.';
                END IF;
            END $$;
            """);

        migrationBuilder.AlterColumn<decimal>(
            name: "DosageAmount", table: "medications", type: "numeric(10,3)", nullable: false,
            oldClrType: typeof(decimal), oldType: "numeric(10,3)", oldNullable: true);
        migrationBuilder.AlterColumn<short>(
            name: "DosageUnit", table: "medications", type: "smallint", nullable: false,
            oldClrType: typeof(short), oldType: "smallint", oldNullable: true);
        migrationBuilder.AddCheckConstraint(
            name: "CK_medications_DosageAmount_positive", table: "medications", sql: "\"DosageAmount\" > 0");
        migrationBuilder.AddCheckConstraint(
            name: "CK_medications_DosageUnit_valid", table: "medications", sql: "\"DosageUnit\" IN (0, 1)");
        migrationBuilder.DropColumn(name: "Dosage", table: "medications");
    }

    protected override void Down(MigrationBuilder migrationBuilder)
    {
        migrationBuilder.AddColumn<string>(
            name: "Dosage", table: "medications", type: "character varying(50)",
            maxLength: 50, nullable: true);
        migrationBuilder.Sql("""
            UPDATE medications SET "Dosage" = "DosageAmount"::text ||
                CASE WHEN "DosageUnit" = 0 THEN ' mg' ELSE ' pastillas' END;
            """);
        migrationBuilder.AlterColumn<string>(
            name: "Dosage", table: "medications", type: "character varying(50)",
            maxLength: 50, nullable: false, oldClrType: typeof(string),
            oldType: "character varying(50)", oldMaxLength: 50, oldNullable: true);
        migrationBuilder.DropCheckConstraint(name: "CK_medications_DosageUnit_valid", table: "medications");
        migrationBuilder.DropCheckConstraint(name: "CK_medications_DosageAmount_positive", table: "medications");
        migrationBuilder.DropColumn(name: "DosageUnit", table: "medications");
        migrationBuilder.DropColumn(name: "DosageAmount", table: "medications");
        migrationBuilder.DropColumn(name: "Message", table: "dose_schedules");
    }
}
