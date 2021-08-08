function f()
    print ("In function f")
end

for i = 1, 10 do
    f()
end

print (i)

local function g(a)
    print ("In function g")
    if a == 12 then
        print ("a is 12")
    else
        print ("a is " .. a)
    end

    local b = a % 2
    if b == 0 then
        return 12
    else
        return 13
    end
end

memory()

local r1 = g(12)
local r2 = g(13)

print (r1)
print (r2)

print(g(14))
